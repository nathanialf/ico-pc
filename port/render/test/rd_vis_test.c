/* rd_vis_test.c: the scene draws' visibility test (port/render/rd_vis.c),
 * CPU only, without a device (rd__init_record_only for the frame cases):
 *
 *   bounds    rd__vis_box over synthetic prelit (3 qwords a vertex), lit (4)
 *             and skinned (5) streams: x, y, z and the pos.w range, the
 *             vertices past a sheet's own count (its tiles) inside, a
 *             position not finite refused; rd__vis_grid_box over a Mesh3D
 *             buffer (the header and MSCNT qwords left out, a buffer too
 *             short refused); rd__vis_skin's boxes by bone slot, the
 *             largest |w0 + w1 - 1| and a weight outside [0, 1] refused
 *   margins   an orthographic block (h = (x, y, z, 1)): a box 2.5 GS pixels
 *             past each side of the target skipped beyond that side, one
 *             1.5 pixels past (inside the margin) kept; the pos.w range
 *             under code 36; a box crossing w = 0 kept, one behind the eye
 *             skipped (code 32) and kept (code 36: its unclipped triangles
 *             are drawn at their GS positions whatever w is)
 *   planes    random cameras (a perspective GS projection, a rotation and a
 *             place) and boxes around the frame, at four frame sizes: every
 *             verdict is seen, and for every skip every corner and 64
 *             points inside the box, through the program's float product
 *             in the VU's order, lie beyond both planes of the side (or
 *             behind the eye); the same for code 36's w range; in both
 *             rounding modes
 *   skinned   two bones: the blend inside the frame kept though one bone's
 *             box is outside, the blend outside skipped; random bones and
 *             weights (summing to 1, and not) checked as above through
 *             vu_skin.hlsl's product; a weight out of range, a bone past the
 *             payload's and a negative region bound keep the draw
 *   oracle    random (box, a strip of vertices inside it, the block,
 *             gsW x gsH): where the test skips, vu1_ref's normal_c (code 32,
 *             and code 36 with mem[16..19] the viewport times mem[20..23])
 *             puts every output vertex in front of the eye beyond the side
 *             (behind the eye for code 32's near verdict) and every fan
 *             vertex of code 36 beyond it, in both rounding modes
 *   frame     rd__vis_frame on a recorded frame: the commands skipped
 *             become RDC_NOPs in a shadow copy with every other byte and
 *             every other list kept, the kept ones as they were (code 34,
 *             particles, lists 11 and 12, no target); rd__vis_culled and
 *             rd__vis_source give the frame as recorded; the frame itself
 *             returned with the mode off, a pass window, a frame never
 *             recorded (number 0) or nothing skipped; a stream written
 *             again (a new serial) bounded again
 *   occlusion the Hi-Z (rd_vis_hiz.c): random cameras' vertices through
 *             the float product in both rounding modes, every one the test
 *             takes inside the region test (or without a clip flag) with
 *             its ftoi4 position within r of the centre and its GS Z above
 *             the bound; random boxes, every corner and 64 points inside
 *             within the occludee's box and under its Z bound; random
 *             triangles rasterised, every covered cell's square (grown by
 *             a pixel each way and the half line) inside every placement
 *             of the corners by 0.25 GS pixel; the inner rule at a cell's
 *             corner (0.3 pixel in counts, 0.2 not); a wall then a box
 *             behind it hidden, one GS Z unit in front, coplanar, inside
 *             the margin, the later mark's room and a half-covered tile
 *             kept; the margin near 2^31 (hidden exactly when the bound
 *             clears it); on a recorded frame under full: the draws behind
 *             an opaque wall NOPs, kept behind a wall under an alpha test
 *             its As may fail, ABE, Z write off or a chain plane, kept
 *             under their own Z test ALWAYS, after a clear or a draw that
 *             may lower the depth; an ALWAYS wall hides, and so does a
 *             wall under ATE GREATER 0x40 whose As is 127 throughout (the
 *             alpha plan's range); the budget (none, one triangle); a
 *             wall whose texture is not uploaded yet (dirty) proven opaque
 *             by the scan the upload will make, a cut-out one not. */
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_replay.h"
#include "rd_vis.h"
#include "shader_consts.h"
#include "vu1_ref.h"
#include "../../test/ico_check.h"

/* ---------------------------------------------------------------- helpers */

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 16);
}

/* uniform in [a, b] */
static double uni(double a, double b)
{
    return a + (b - a) * (double)(rnd() & 0xFFFFFF) / (double)0xFFFFFF;
}

/* the program's product in the VU's order (vu_common.hlsli vu_mat), every
 * product and sum rounded to float on its own (-ffp-contract=off) */
static void vuMat(const float (*m)[4], const float v[3], float w, float h[4])
{
    for (int c = 0; c < 4; c++) {
        float a = m[0][c] * v[0];
        a = a + m[1][c] * v[1];
        a = a + m[2][c] * v[2];
        a = a + m[3][c] * w;
        h[c] = a;
    }
}

/* whether the float h lies beyond verdict v's planes: both margins of a
 * side (the values in double from the float lanes), or behind the eye */
static bool hBeyond(int v, const float h[4], const RdVisRect *r)
{
    const double x = h[0], y = h[1], w = h[3];
    const double e[2] = {RD_VIS_MARGIN, RD_VIS_MARGIN_IN};
    for (int k = 0; k < 2; k++) {
        double val;
        switch (v) {
        case RD_VIS_LEFT:
            val = x - (r->x0 - e[k]) * w;
            break;
        case RD_VIS_RIGHT:
            val = (r->x1 + e[k]) * w - x;
            break;
        case RD_VIS_TOP:
            val = y - (r->y0 - e[k]) * w;
            break;
        case RD_VIS_BOTTOM:
            val = (r->y1 + e[k]) * w - y;
            break;
        case RD_VIS_NEAR:
            val = w;
            break;
        default:
            return false;
        }
        if (!(val < 0.0)) {
            return false;
        }
    }
    return true;
}

/* a GS position (ftoi4 X, Y as the GS keeps them before the 16-bit field)
 * beyond the side of verdict v of the rectangle itself (no margin) */
static bool gsBeyond(int v, int32_t gx, int32_t gy, const RdVisRect *r)
{
    const double x = (double)gx / 16.0, y = (double)gy / 16.0;
    switch (v) {
    case RD_VIS_LEFT:
        return x < r->x0;
    case RD_VIS_RIGHT:
        return x > r->x1;
    case RD_VIS_TOP:
        return y < r->y0;
    case RD_VIS_BOTTOM:
        return y > r->y1;
    default:
        return false;
    }
}

static void boxOf(RdVisBox *b, const float lo[4], const float hi[4])
{
    memcpy(b->lo, lo, sizeof(b->lo));
    memcpy(b->hi, hi, sizeof(b->hi));
    b->n = 2;
}

/* a point of the box: corner k (k < 16: x, y, z by its bits, w by bit 3) or
 * a random one inside */
static void boxPoint(const RdVisBox *b, int k, float p[4])
{
    for (int i = 0; i < 4; i++) {
        if (k < 16) {
            p[i] = (k >> i) & 1 ? b->hi[i] : b->lo[i];
        } else {
            p[i] = (float)uni(b->lo[i], b->hi[i]);
        }
    }
}

/* ----------------------------------------------------------------- cameras
 * view = R p + T; the GS projection x' = f X + 2048 Z, y' = f Y + 2048 Z,
 * z' = 1000 Z + 5, w' = Z (vu1_test.c's form with a focal length): the
 * columns of M = P [R | T] */
typedef struct Cam {
    double r[3][3], t[3], f;
} Cam;

static void randomRotation(double r[3][3])
{
    double q[4], n = 0.0;
    for (int i = 0; i < 4; i++) {
        q[i] = uni(-1.0, 1.0);
        n += q[i] * q[i];
    }
    n = sqrt(n > 1e-12 ? n : 1.0);
    const double a = q[0] / n, b = q[1] / n, c = q[2] / n, d = q[3] / n;
    r[0][0] = a * a + b * b - c * c - d * d;
    r[0][1] = 2 * (b * c - a * d);
    r[0][2] = 2 * (b * d + a * c);
    r[1][0] = 2 * (b * c + a * d);
    r[1][1] = a * a - b * b + c * c - d * d;
    r[1][2] = 2 * (c * d - a * b);
    r[2][0] = 2 * (b * d - a * c);
    r[2][1] = 2 * (c * d + a * b);
    r[2][2] = a * a - b * b - c * c + d * d;
}

/* P applied to a view-space vector (x, y, z, w) */
static void proj(const Cam *c, const double v[4], double out[4])
{
    out[0] = c->f * v[0] + 2048.0 * v[2];
    out[1] = c->f * v[1] + 2048.0 * v[2];
    out[2] = 1000.0 * v[2] + 5.0 * v[3];
    out[3] = v[2];
}

static void camMatrix(const Cam *c, float m[4][4])
{
    for (int j = 0; j < 4; j++) {
        double v[4] = {0, 0, 0, 0}, o[4];
        if (j < 3) {
            v[0] = c->r[0][j];
            v[1] = c->r[1][j];
            v[2] = c->r[2][j];
        } else {
            v[0] = c->t[0];
            v[1] = c->t[1];
            v[2] = c->t[2];
            v[3] = 1.0;
        }
        proj(c, v, o);
        for (int k = 0; k < 4; k++) {
            m[j][k] = (float)o[k];
        }
    }
}

/* a camera placing a box's centre (the model's origin) at the screen
 * position (sx, sy) at depth z (a depth not above 1: anywhere across) */
static void randomCam(Cam *c, double sx, double sy, double z)
{
    randomRotation(c->r);
    c->f = uni(150.0, 900.0);
    c->t[2] = z;
    if (z > 1.0) {
        c->t[0] = (sx - 2048.0) * z / c->f;
        c->t[1] = (sy - 2048.0) * z / c->f;
    } else {
        c->t[0] = uni(-500.0, 500.0);
        c->t[1] = uni(-500.0, 500.0);
    }
}

static const uint32_t kFrames[4][2] = {{512, 448}, {688, 448}, {1024, 224}, {2048, 448}};

static void setRound(int mode)
{
    fesetround(mode ? FE_TOWARDZERO : FE_TONEAREST);
}

/* ------------------------------------------------------------------ bounds */

static void checkBounds(void)
{
    /* prelit: pos, ST, colour */
    float pre[5][3][4];
    memset(pre, 0, sizeof(pre));
    const float pos[5][4] = {
        {1, 2, 3, 1}, {-4, 5, 0.5f, 0.25f}, {7, -8, 2, 2}, {0, 0, -9, 1}, {3, 3, 3, 1}};
    for (int v = 0; v < 5; v++) {
        memcpy(pre[v][0], pos[v], 16);
        pre[v][1][3] = 1.0f;
        pre[v][2][0] = 99.0f; /* a colour lane: not a position */
    }
    RdVisBox b;
    CHECK(rd__vis_box((const float (*)[4])pre, 5, 3, &b) && b.n == 5, "prelit bound");
    CHECK(b.lo[0] == -4 && b.hi[0] == 7 && b.lo[1] == -8 && b.hi[1] == 5 && b.lo[2] == -9 &&
              b.hi[2] == 3,
          "prelit: x %g..%g y %g..%g z %g..%g", b.lo[0], b.hi[0], b.lo[1], b.hi[1], b.lo[2],
          b.hi[2]);
    CHECK(b.lo[3] == 0.25f && b.hi[3] == 2.0f, "prelit: the pos.w range %g..%g", b.lo[3], b.hi[3]);
    /* lit: pos, normal, ST, colour; a sheet's tiles (the vertices past its
     * own count) bounded with it */
    float lit[6][4][4];
    memset(lit, 0, sizeof(lit));
    for (int v = 0; v < 6; v++) {
        lit[v][0][0] = (float)v;
        lit[v][0][1] = (float)(v * 2);
        lit[v][0][2] = 1.0f;
        lit[v][0][3] = 1.0f;
        lit[v][1][0] = 1000.0f; /* the normal: not a position */
    }
    lit[5][0][0] = 500.0f; /* the last tile's corner */
    CHECK(rd__vis_box((const float (*)[4])lit, 6, 4, &b) && b.n == 6 && b.hi[0] == 500.0f &&
              b.hi[1] == 10.0f,
          "lit: the tiles inside (%g, %g)", b.hi[0], b.hi[1]);
    lit[2][0][1] = NAN;
    CHECK(!rd__vis_box((const float (*)[4])lit, 6, 4, &b) && b.n == 0, "lit: NaN refused");
    lit[2][0][1] = INFINITY;
    CHECK(!rd__vis_box((const float (*)[4])lit, 6, 4, &b), "lit: infinity refused");
    CHECK(rd__vis_box(NULL, 0, 3, &b) && b.n == 0, "an empty stream: an empty bound");
    /* a grid: per strip the VIF qword, the tag, the colour, 3 vertices of
     * pos and ST, MSCNT */
    float grid[2 * (3 * 2 + 4)][4];
    for (int q = 0; q < 20; q++) {
        for (int k = 0; k < 4; k++) {
            grid[q][k] = 5000.0f; /* header and trailer: never read */
        }
    }
    for (int s = 0; s < 2; s++) {
        for (int k = 0; k < 3; k++) {
            float *p = grid[s * 10 + 3 + k * 2];
            p[0] = (float)(s * 10 + k);
            p[1] = -(float)k;
            p[2] = 0.5f;
            p[3] = 1.0f;
            grid[s * 10 + 3 + k * 2 + 1][0] = 7777.0f; /* ST */
        }
    }
    CHECK(rd__vis_grid_box((const float (*)[4])grid, 20, 2, 3, 2, &b) && b.n == 6 &&
              b.lo[0] == 0.0f && b.hi[0] == 12.0f && b.lo[1] == -2.0f && b.hi[1] == 0.0f,
          "grid: x %g..%g y %g..%g", b.lo[0], b.hi[0], b.lo[1], b.hi[1]);
    CHECK(!rd__vis_grid_box((const float (*)[4])grid, 19, 2, 3, 2, &b),
          "grid: the strips past the buffer refused");
    CHECK(!rd__vis_grid_box((const float (*)[4])grid, 20, 2, 2, 2, &b),
          "grid: two vertices a strip refused");
    /* skinned: pos, normal, weights (address, w0, address, w1), ST, colour */
    float sk[4][5][4];
    memset(sk, 0, sizeof(sk));
    const uint32_t addr[4][2] = {{16, 20}, {20, 16}, {24, 24}, {8, 300}};
    const float wts[4][2] = {{1.0f, 0.0f}, {0.5f, 0.5f}, {0.25f, 0.75f}, {0.6f, 0.3f}};
    for (int v = 0; v < 4; v++) {
        sk[v][0][0] = (float)(v * 10);
        sk[v][0][1] = 1.0f;
        sk[v][0][2] = -(float)v;
        sk[v][0][3] = 1.0f;
        memcpy(&sk[v][2][0], &addr[v][0], 4);
        sk[v][2][1] = wts[v][0];
        memcpy(&sk[v][2][2], &addr[v][1], 4);
        sk[v][2][3] = wts[v][1];
    }
    static RdVisSkin s;
    CHECK(rd__vis_skin((const float (*)[4])sk, 4, 5, &s) && s.ok, "skin bound");
    /* slots: 16 -> 0, 20 -> 4, 24 -> 8, 8 -> 236 (unsigned), 300 -> 236 */
    CHECK((s.used[0] & 0x111u) == 0x111u && (s.used[236 >> 5] & (1u << (236 & 31))),
          "skin: the slots named");
    CHECK(s.box[0].n == 2 && s.box[0].lo[0] == 0.0f && s.box[0].hi[0] == 10.0f,
          "skin: slot 0 holds vertices 0 and 1 (%u, %g..%g)", s.box[0].n, s.box[0].lo[0],
          s.box[0].hi[0]);
    CHECK(s.box[236].n == 2 && s.box[236].lo[0] == 30.0f, "skin: slot 236 holds vertex 3 twice");
    CHECK(s.qwEnd == 240u, "skin: the qwords read end at %u", s.qwEnd);
    CHECK(fabs(s.emax - 0.1) < 1e-6, "skin: the largest |w0 + w1 - 1| %g", s.emax);
    sk[2][2][1] = 1.5f;
    CHECK(!rd__vis_skin((const float (*)[4])sk, 4, 5, &s) && !s.ok, "skin: a weight above 1");
    sk[2][2][1] = -0.01f;
    CHECK(!rd__vis_skin((const float (*)[4])sk, 4, 5, &s) && !s.ok, "skin: a weight below 0");
    sk[2][2][1] = NAN;
    CHECK(!rd__vis_skin((const float (*)[4])sk, 4, 5, &s) && !s.ok, "skin: a NaN weight");
    printf("  bounds: prelit, lit with tiles, grid, skinned slots\n");
}

/* ----------------------------------------------------------------- margins */

static const float kOrtho[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

static void checkMargins(void)
{
    const RdVisRect r = rd__vis_rect(512, 448, 512, 448);
    CHECK(r.x0 == 1792.0 && r.x1 == 2304.0 && r.y0 == 1824.0 && r.y1 == 2273.0,
          "the 512 x 448 frame's rectangle %g..%g, %g..%g", r.x0, r.x1, r.y0, r.y1);
    const RdVisRect wr = rd__vis_rect(512, 448, 600, 400);
    CHECK(wr.x1 == 1792.0 + 600.0 && wr.y1 == 1824.0 + 449.0,
          "a target record wider than the state's size: its width");
    RdVisBox b;
    const float my = 2048.0f, mz = 0.5f;

    struct {
        float lo[2], hi[2];
        int want;
    } cases[] = {
        {{1700.0f, my}, {1792.0f - 2.5f, my + 10}, RD_VIS_LEFT},
        {{1700.0f, my}, {1792.0f - 1.5f, my + 10}, RD_VIS_KEEP},
        {{2304.0f + 2.5f, my}, {2400.0f, my + 10}, RD_VIS_RIGHT},
        {{2304.0f + 1.5f, my}, {2400.0f, my + 10}, RD_VIS_KEEP},
        {{2000.0f, 1700.0f}, {2010.0f, 1824.0f - 2.5f}, RD_VIS_TOP},
        {{2000.0f, 1700.0f}, {2010.0f, 1824.0f - 1.5f}, RD_VIS_KEEP},
        {{2000.0f, 2273.0f + 2.5f}, {2010.0f, 2400.0f}, RD_VIS_BOTTOM},
        {{2000.0f, 2273.0f + 1.5f}, {2010.0f, 2400.0f}, RD_VIS_KEEP},
        {{2000.0f, 2000.0f}, {2010.0f, 2010.0f}, RD_VIS_KEEP},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const float lo[4] = {cases[i].lo[0], cases[i].lo[1], mz, 1.0f};
        const float hi[4] = {cases[i].hi[0], cases[i].hi[1], mz, 1.0f};
        boxOf(&b, lo, hi);
        const int v = rd__vis_test(kOrtho, &b, false, &r);
        CHECK(v == cases[i].want, "margins case %zu: verdict %d, want %d", i, v, cases[i].want);
        const int vs = rd__vis_test(kOrtho, &b, true, &r);
        CHECK(vs == cases[i].want, "margins case %zu, code 36: verdict %d, want %d", i, vs,
              cases[i].want);
    }
    /* code 36 reads pos.w: x / w over w 0.5 .. 1 reaches 1700 / 0.5 */
    {
        const float lo[4] = {1700.0f, my, mz, 0.5f}, hi[4] = {1750.0f, my + 10, mz, 1.0f};
        boxOf(&b, lo, hi);
        CHECK(rd__vis_test(kOrtho, &b, false, &r) == RD_VIS_LEFT, "w 1: left");
        CHECK(rd__vis_test(kOrtho, &b, true, &r) == RD_VIS_KEEP,
              "code 36: x / pos.w reaches the frame, kept");
    }
    /* w = z: a box crossing w = 0 is kept; one behind the eye is skipped
     * under code 32, kept under code 36 */
    const float wz[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}};
    {
        const float lo[4] = {-1, -1, -10, 1}, hi[4] = {1, 1, 10, 1};
        boxOf(&b, lo, hi);
        CHECK(rd__vis_test(wz, &b, false, &r) == RD_VIS_KEEP, "crossing w = 0: kept");
        const float lo2[4] = {-1, -1, -30, 1}, hi2[4] = {1, 1, -10, 1};
        boxOf(&b, lo2, hi2);
        CHECK(rd__vis_test(wz, &b, false, &r) == RD_VIS_NEAR, "behind the eye: skipped");
        CHECK(rd__vis_test(wz, &b, true, &r) == RD_VIS_KEEP, "behind the eye, code 36: kept");
        const float lo3[4] = {-1, -1, -30, 1}, hi3[4] = {1, 1, 0, 1};
        boxOf(&b, lo3, hi3);
        CHECK(rd__vis_test(wz, &b, false, &r) == RD_VIS_KEEP, "w reaching 0: kept");
    }
    /* a matrix with a NaN, an empty box: kept */
    {
        float bad[4][4];
        memcpy(bad, kOrtho, sizeof(bad));
        bad[3][0] = NAN;
        const float lo[4] = {1700.0f, my, mz, 1.0f}, hi[4] = {1750.0f, my + 10, mz, 1.0f};
        boxOf(&b, lo, hi);
        CHECK(rd__vis_test((const float (*)[4])bad, &b, false, &r) == RD_VIS_KEEP, "NaN: kept");
        b.n = 0;
        CHECK(rd__vis_test(kOrtho, &b, false, &r) == RD_VIS_KEEP, "an empty box: kept");
    }
    printf("  margins: 2.5 pixels past each side skipped, 1.5 kept, w = 0 and code 36's w\n");
}

/* ------------------------------------------------------------------ planes */

static void checkPlanes(void)
{
    uint32_t seen[2][RD_VIS_VERDICTS];
    memset(seen, 0, sizeof(seen));
    uint32_t bad = 0, points = 0;
    for (int mode = 0; mode < 2; mode++) {
        setRound(mode);
        for (int n = 0; n < 6000; n++) {
            const uint32_t *fs = kFrames[rnd() & 3];
            const RdVisRect r = rd__vis_rect(fs[0], fs[1], fs[0], fs[1]);
            Cam c;
            const double z = (rnd() & 7) == 0 ? uni(-400.0, 0.0) : uni(5.0, 3000.0);
            randomCam(&c, uni(-2000.0, 6000.0), uni(-2000.0, 6000.0), z);
            float m[4][4];
            camMatrix(&c, m);
            RdVisBox b;
            float lo[4], hi[4];
            for (int i = 0; i < 3; i++) {
                const double h = uni(0.5, 200.0);
                lo[i] = (float)(-h * uni(0.2, 1.0));
                hi[i] = (float)(h * uni(0.2, 1.0));
            }
            lo[3] = (float)uni(0.3, 1.0);
            hi[3] = (float)uni(1.0, 2.0);
            boxOf(&b, lo, hi);
            for (int sc = 0; sc < 2; sc++) {
                const int v = rd__vis_test((const float (*)[4])m, &b, sc != 0, &r);
                seen[sc][v]++;
                if (v == RD_VIS_KEEP) {
                    continue;
                }
                CHECK(!(sc && v == RD_VIS_NEAR), "code 36 skipped behind the eye");
                for (int k = 0; k < 16 + 64; k++) {
                    float p[4], h[4];
                    boxPoint(&b, k, p);
                    vuMat((const float (*)[4])m, p, sc ? p[3] : 1.0f, h);
                    points++;
                    const bool front = !sc || h[3] > 0.0f;
                    if (!hBeyond(v, h, &r) || !front) {
                        bad++;
                    }
                }
            }
        }
    }
    setRound(0);
    CHECK(bad == 0, "planes: %u of %u points of skipped boxes not beyond their planes", bad,
          points);
    /* the plane test's verdicts (RD_VIS_OCCLUDED is the occlusion test's,
     * never rd__vis_test's) */
    for (int v = 0; v <= RD_VIS_NEAR; v++) {
        CHECK(seen[0][v] > 0, "planes: verdict %d never seen (code 32)", v);
        CHECK(v == RD_VIS_NEAR || seen[1][v] > 0, "planes: verdict %d never seen (code 36)", v);
    }
    printf("  planes: keep %u, left %u, right %u, top %u, bottom %u, behind %u (code 32); %u "
           "points checked\n",
           seen[0][0], seen[0][1], seen[0][2], seen[0][3], seen[0][4], seen[0][5], points);
}

/* ----------------------------------------------------------------- skinned */

/* vu_skin.hlsl vu_skin_vertex's h: the two bones, blended, through mem[4..7] */
static void skinH(const float (*mem)[4], const float (*bones)[4], const float *pos, const float *wq,
                  float h[4])
{
    uint32_t a[2];
    memcpy(&a[0], &wq[0], 4);
    memcpy(&a[1], &wq[2], 4);
    float p[2][4];
    for (int k = 0; k < 2; k++) {
        uint32_t i = a[k] - 16u;
        i = i < 236u ? i : 236u;
        vuMat(&bones[i], pos, 1.0f, p[k]);
    }
    float pb[3];
    for (int i = 0; i < 3; i++) {
        float x = p[0][i] * wq[1];
        x = x + p[1][i] * wq[3];
        pb[i] = x;
    }
    vuMat(&mem[4], pb, 1.0f, h);
}

static void setAddr(float *wq, uint32_t a0, float w0, uint32_t a1, float w1)
{
    memcpy(&wq[0], &a0, 4);
    wq[1] = w0;
    memcpy(&wq[2], &a1, 4);
    wq[3] = w1;
}

static void checkSkinned(void)
{
    static float mem[36][4];
    static float bones[240][4];
    static RdVisSkin s;
    const RdVisRect r = rd__vis_rect(512, 448, 512, 448);
    /* two bones translating +-50 across, half and half: the blend is the
     * position; positions 1600..1700 across: left of 1792 */
    memset(mem, 0, sizeof(mem));
    memset(bones, 0, sizeof(bones));
    memcpy(&mem[4], kOrtho, sizeof(kOrtho));
    for (int b = 0; b < 2; b++) {
        memcpy(&bones[b * 4], kOrtho, sizeof(kOrtho));
        bones[b * 4 + 3][0] = b ? -50.0f : 50.0f;
    }
    float st[8][5][4];
    memset(st, 0, sizeof(st));
    for (int v = 0; v < 8; v++) {
        st[v][0][0] = 1600.0f + (float)(v * 14);
        st[v][0][1] = 2048.0f + (float)v;
        st[v][0][2] = 0.5f;
        st[v][0][3] = 1.0f;
        setAddr(st[v][2], 16, 0.5f, 20, 0.5f);
    }
    CHECK(rd__vis_skin((const float (*)[4])st, 8, 5, &s) && s.ok && s.qwEnd == 8u, "skin bound");
    int v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones, 8, &s, &r);
    CHECK(v == RD_VIS_LEFT, "two bones, the blend left of the frame: verdict %d", v);
    /* bone 0 far right (+1000): the blend 1600 + 475 .. reaches the frame,
     * though bone 1's box alone (1550..) lies left of it */
    bones[3][0] = 1000.0f;
    v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones, 8, &s, &r);
    CHECK(v == RD_VIS_KEEP, "two bones, the blend inside: verdict %d", v);
    bones[3][0] = 50.0f;
    /* the bones past the payload's, a negative region bound: kept */
    v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones, 7, &s, &r);
    CHECK(v == RD_VIS_KEEP, "a bone past the payload's: verdict %d", v);
    mem[0][3] = -1.0f;
    v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones, 8, &s, &r);
    CHECK(v == RD_VIS_KEEP, "mem[0].w below 0: verdict %d", v);
    mem[0][3] = 0.0f;
    /* weights summing to 0.6 (the blend 0.6 of the way from the origin,
     * 0 + 0.6 x, which is inside for x near 3200): the origin's share */
    for (int k = 0; k < 8; k++) {
        st[k][0][0] = 3300.0f + (float)k;
        setAddr(st[k][2], 16, 0.3f, 20, 0.3f);
    }
    CHECK(rd__vis_skin((const float (*)[4])st, 8, 5, &s) && s.ok, "skin bound (0.6)");
    v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones, 8, &s, &r);
    CHECK(v == RD_VIS_KEEP, "weights summing to 0.6 near the frame: verdict %d", v);
    /* random bones and weights, through vu_skin.hlsl's product */
    uint32_t seen[RD_VIS_VERDICTS] = {0}, bad = 0, points = 0;
    for (int mode = 0; mode < 2; mode++) {
        setRound(mode);
        for (int n = 0; n < 3000; n++) {
            const uint32_t *fs = kFrames[rnd() & 3];
            const RdVisRect rr = rd__vis_rect(fs[0], fs[1], fs[0], fs[1]);
            Cam c;
            const double z = (rnd() & 7) == 0 ? uni(-400.0, 0.0) : uni(5.0, 3000.0);
            randomCam(&c, uni(-2000.0, 6000.0), uni(-2000.0, 6000.0), z);
            float m[4][4];
            camMatrix(&c, m);
            memset(mem, 0, sizeof(mem));
            memcpy(&mem[4], m, sizeof(m));
            const int nb = 1 + (int)(rnd() % 4);
            for (int b = 0; b < nb; b++) {
                double rot[3][3];
                randomRotation(rot);
                for (int j = 0; j < 3; j++) {
                    for (int k = 0; k < 3; k++) {
                        bones[b * 4 + j][k] = (float)rot[k][j];
                    }
                    bones[b * 4 + j][3] = 0.0f;
                    bones[b * 4 + 3][j] = (float)uni(-60.0, 60.0);
                }
                bones[b * 4 + 3][3] = 1.0f;
            }
            const bool partition = (rnd() & 3) != 0;
            float sv[24][5][4];
            memset(sv, 0, sizeof(sv));
            for (int k = 0; k < 24; k++) {
                for (int i = 0; i < 3; i++) {
                    sv[k][0][i] = (float)uni(-40.0, 40.0);
                }
                sv[k][0][3] = 1.0f;
                const float w0 = (float)uni(0.0, 1.0);
                const float w1 = partition ? 1.0f - w0 : (float)uni(0.0, 1.0 - w0 * 0.5);
                setAddr(sv[k][2], 16u + 4u * (rnd() % (uint32_t)nb), w0,
                        16u + 4u * (rnd() % (uint32_t)nb), w1 > 1.0f ? 1.0f : w1);
            }
            if (!rd__vis_skin((const float (*)[4])sv, 24, 5, &s)) {
                continue;
            }
            v = rd__vis_test_skin((const float (*)[4])mem, (const float (*)[4])bones,
                                  (uint32_t)nb * 4u, &s, &rr);
            seen[v]++;
            if (v == RD_VIS_KEEP) {
                continue;
            }
            for (int k = 0; k < 24; k++) {
                float h[4];
                skinH((const float (*)[4])mem, (const float (*)[4])bones, sv[k][0], sv[k][2], h);
                points++;
                bad += !hBeyond(v, h, &rr);
            }
        }
    }
    setRound(0);
    CHECK(bad == 0, "skinned: %u of %u vertices of skipped draws not beyond their planes", bad,
          points);
    CHECK(seen[RD_VIS_KEEP] > 0 &&
              seen[RD_VIS_LEFT] + seen[RD_VIS_RIGHT] + seen[RD_VIS_TOP] + seen[RD_VIS_BOTTOM] > 0,
          "skinned: no side skip seen");
    CHECK(seen[RD_VIS_NEAR] > 0, "skinned: no skip behind the eye seen");
    printf("  skinned: keep %u, sides %u, behind %u; %u vertices checked\n", seen[0],
           seen[1] + seen[2] + seen[3] + seen[4], seen[5], points);
}

/* ------------------------------------------------------------------ oracle
 * vu1_ref's normal_c on a strip inside the box, where the test skips it */

#define OR_VERTS 24

static void checkOracle(void)
{
    static Vu1Ref ref;
    static VuBatchOut out;
    float in[1 + OR_VERTS * 3][4];
    uint32_t seen[2] = {0, 0}, verts = 0, fans = 0, bad = 0;
    for (int mode = 0; mode < 2; mode++) {
        for (int n = 0; n < 8000; n++) {
            setRound(0);
            const uint32_t *fs = kFrames[rnd() & 3];
            const RdVisRect r = rd__vis_rect(fs[0], fs[1], fs[0], fs[1]);
            Cam c;
            const double z = (rnd() & 7) == 0 ? uni(-400.0, 0.0) : uni(5.0, 3000.0);
            randomCam(&c, uni(-2000.0, 6000.0), uni(-2000.0, 6000.0), z);
            /* clip C: (f X / G, f Y / G, Z - 1, Z); the viewport V: (G cx +
             * 2048 cw, G cy + 2048 cw, 1000 cz + 1005 cw, cw); M = V C [R|T] */
            const double g = 2048.0;
            float m[4][4], cm[4][4], vp[4][4];
            camMatrix(&c, m);
            for (int j = 0; j < 4; j++) {
                const double vx = j < 3 ? c.r[0][j] : c.t[0], vy = j < 3 ? c.r[1][j] : c.t[1];
                const double vz = j < 3 ? c.r[2][j] : c.t[2], vw = j < 3 ? 0.0 : 1.0;
                cm[j][0] = (float)(c.f * vx / g);
                cm[j][1] = (float)(c.f * vy / g);
                cm[j][2] = (float)(vz - vw);
                cm[j][3] = (float)vz;
            }
            memset(vp, 0, sizeof(vp));
            vp[0][0] = (float)g;
            vp[1][1] = (float)g;
            vp[2][2] = 1000.0f;
            vp[3][0] = 2048.0f;
            vp[3][1] = 2048.0f;
            vp[3][2] = 1005.0f;
            vp[3][3] = 1.0f;
            /* the vertices: a strip inside a random box */
            float lo[3], hi[3];
            for (int i = 0; i < 3; i++) {
                const double h = uni(0.5, 150.0);
                lo[i] = (float)(-h * uni(0.2, 1.0));
                hi[i] = (float)(h * uni(0.2, 1.0));
            }
            float stream[OR_VERTS][3][4];
            memset(stream, 0, sizeof(stream));
            for (int k = 0; k < OR_VERTS; k++) {
                for (int i = 0; i < 3; i++) {
                    stream[k][0][i] =
                        k < 8 ? ((k >> i) & 1 ? hi[i] : lo[i]) : (float)uni(lo[i], hi[i]);
                }
                stream[k][0][3] = 1.0f;
                stream[k][1][0] = (float)k;
                stream[k][1][2] = 1.0f;
                stream[k][1][3] = k == 0 || (rnd() & 7) == 0 ? 0.0f : 1.0f; /* the strip flag */
                stream[k][2][0] = stream[k][2][1] = stream[k][2][2] = 128.0f;
                stream[k][2][3] = 127.0f;
            }
            RdVisBox b;
            if (!rd__vis_box((const float (*)[4])stream, OR_VERTS, 3, &b)) {
                continue;
            }
            const int code = (n & 1) ? 36 : 32;
            const int v = rd__vis_test((const float (*)[4])m, &b, code == 36, &r);
            if (v == RD_VIS_KEEP) {
                continue;
            }
            seen[code == 36]++;
            /* the block: the common (qw 3 the GIF tag the fans take, 8..11
             * the viewport), then SET_NORMAL_MATRIX's M, C and four more */
            float common[16][4];
            memset(common, 0, sizeof(common));
            common[0][3] = 1.0f;
            const uint32_t tag[4] = {0x8000u, 0x302EC000u, 0x512u, 0u};
            memcpy(common[3], tag, sizeof(tag));
            memcpy(&common[8], vp, sizeof(vp));
            float nm[12][4];
            memset(nm, 0, sizeof(nm));
            memcpy(&nm[0], m, sizeof(m));
            memcpy(&nm[4], cm, sizeof(cm));
            setRound(mode);
            vu1ref_init(&ref);
            vu1ref_load_common(&ref, (const float (*)[4])common);
            vu1ref_normal_set_matrix(&ref, (const float (*)[4])nm);
            uint32_t nloop = OR_VERTS;
            memcpy(in[0], &nloop, 4);
            in[0][1] = in[0][2] = in[0][3] = 0.0f;
            memcpy(in[1], stream, sizeof(stream));
            memset(&out, 0, sizeof(out));
            vu1ref_normal_c(&ref, code, (const float (*)[4])in, &out);
            setRound(0);
            for (int k = 0; k < out.count; k++) {
                const VuGsVertex *o = &out.v[k];
                verts++;
                if (v == RD_VIS_NEAR) {
                    bad += !(o->clipW <= 0.0f);
                } else if (code == 32 && !(o->clipW > 0.0f)) {
                    /* behind the eye: no GS position (the port clips it) */
                } else {
                    bad += !gsBeyond(v, o->xyz[0], o->xyz[1], &r);
                }
            }
            for (int fi = 0; fi < out.fanCount; fi++) {
                for (int k = 0; k < out.fans[fi].n; k++) {
                    fans++;
                    bad += !gsBeyond(v, out.fans[fi].v[k].xyz[0], out.fans[fi].v[k].xyz[1], &r);
                }
            }
        }
    }
    setRound(0);
    CHECK(bad == 0, "oracle: %u of %u vertices and %u fan vertices on the frame's side", bad, verts,
          fans);
    CHECK(seen[0] >= 100 && seen[1] >= 100, "oracle: %u code-32 and %u code-36 skips", seen[0],
          seen[1]);
    printf("  oracle: %u code-32 and %u code-36 skips, %u vertices and %u fan vertices beyond\n",
           seen[0], seen[1], verts, fans);
}

/* --------------------------------------------------------------- occlusion */

/* vu_ftoi4 on the CPU: truncation, saturating (a NaN to INT32_MIN) */
static int32_t ftoi4f(float x)
{
    const float y = x * 16.0f;
    if (!(y >= -2147483648.0f)) {
        return INT32_MIN;
    }
    if (y >= 2147483648.0f) {
        return INT32_MAX;
    }
    return (int32_t)y;
}

/* code 36's clip space for a camera matrix: x and y about the GS centre
 * over 1024 GS units of w, z 0, w the same */
static void clipOf(const float m[4][4], float c[4][4])
{
    for (int j = 0; j < 4; j++) {
        c[j][0] = (m[j][0] - 2048.0f * m[j][3]) / 1024.0f;
        c[j][1] = (m[j][1] - 2048.0f * m[j][3]) / 1024.0f;
        c[j][2] = 0.0f;
        c[j][3] = m[j][3];
    }
}

static void checkHizVertex(void)
{
    uint32_t seen = 0, sc36 = 0, bad = 0;
    for (int round = 0; round < 2; round++) {
        setRound(round);
        for (int n = 0; n < 4000; n++) {
            Cam c;
            randomCam(&c, uni(1500.0, 2600.0), uni(1600.0, 2500.0), uni(40.0, 4000.0));
            float m[4][4], cl[4][4];
            camMatrix(&c, m);
            clipOf(m, cl);
            const bool scissor = (n & 3) == 3;
            const float pos[4] = {(float)uni(-200.0, 200.0), (float)uni(-200.0, 200.0),
                                  (float)uni(-200.0, 200.0), scissor ? (float)uni(0.5, 2.0) : 1.0f};
            RdVisHizVtx v;
            if (!rd__vis_hiz_vertex((const float (*)[4])m, scissor ? (const float (*)[4])cl : NULL,
                                    pos, &v)) {
                continue;
            }
            seen++;
            sc36 += scissor;
            const float w = scissor ? pos[3] : 1.0f;
            float h[4];
            vuMat((const float (*)[4])m, pos, w, h);
            const float q = 1.0f / h[3];
            const float px = h[0] * q, py = h[1] * q, pz = h[2] * q;
            const int32_t gx = ftoi4f(px), gy = ftoi4f(py), gz = ftoi4f(pz);
            bool ok = h[3] > 0.0f && h[3] < 16777214.0f && px > 0.0f && px < 4094.0f && py > 0.0f &&
                      py < 4094.0f && gz >= 0 && gz != INT32_MAX;
            /* the GS position and ICO_RD_VU_OFFGRID's p within r */
            ok = ok && fabs((double)gx / 16.0 - v.x) <= v.r &&
                 fabs((double)gy / 16.0 - v.y) <= v.r && fabs((double)px - v.x) <= v.r &&
                 fabs((double)py - v.y) <= v.r && (double)gz >= v.zlo;
            if (scissor) {
                float cc[4];
                vuMat((const float (*)[4])cl, pos, pos[3], cc);
                const float aw = fabsf(cc[3]);
                ok = ok && !(cc[0] > aw || cc[0] < -aw || cc[1] > aw || cc[1] < -aw || cc[2] > aw ||
                             cc[2] < -aw);
            }
            if (!ok && bad++ < 5) {
                printf("    vertex %d: GS (%d, %d, %d) against (%g, %g) r %g, Z >= %g\n", n, gx, gy,
                       gz, v.x, v.y, v.r, v.zlo);
            }
        }
    }
    setRound(0);
    CHECK(bad == 0 && seen > 1000u && sc36 > 100u,
          "occlusion: %u of %u vertices taken (%u code 36) not where the bound says", bad, seen,
          sc36);
    printf("  occlusion: %u vertices taken (%u under code 36), each at its bound\n", seen, sc36);
}

static void checkHizOccludee(void)
{
    uint32_t seen = 0, pts = 0, bad = 0;
    for (int round = 0; round < 2; round++) {
        setRound(round);
        for (int n = 0; n < 3000; n++) {
            Cam c;
            randomCam(&c, uni(1700.0, 2400.0), uni(1800.0, 2300.0), uni(100.0, 4000.0));
            float m[4][4];
            camMatrix(&c, m);
            const bool scissor = (n & 3) == 3;
            const float lo[4] = {(float)-uni(1.0, 80.0), (float)-uni(1.0, 80.0),
                                 (float)-uni(1.0, 80.0), scissor ? (float)uni(0.5, 1.0) : 1.0f};
            const float hi[4] = {(float)uni(1.0, 80.0), (float)uni(1.0, 80.0),
                                 (float)uni(1.0, 80.0), scissor ? (float)uni(1.0, 2.0) : 1.0f};
            RdVisBox b;
            boxOf(&b, lo, hi);
            RdVisHizBox o;
            if (!rd__vis_hiz_occludee((const float (*)[4])m, &b, scissor, &o)) {
                continue;
            }
            seen++;
            for (int k = 0; k < 16 + 64; k++) {
                float p[4];
                boxPoint(&b, k, p);
                float h[4];
                vuMat((const float (*)[4])m, p, scissor ? p[3] : 1.0f, h);
                const float q = 1.0f / h[3];
                const float px = h[0] * q, py = h[1] * q, pz = h[2] * q;
                const int32_t gx = ftoi4f(px), gy = ftoi4f(py), gz = ftoi4f(pz);
                pts++;
                const bool ok = (double)gx / 16.0 >= o.x0 && (double)px <= o.x1 &&
                                (double)gy / 16.0 >= o.y0 && (double)py <= o.y1 &&
                                16.0 * (double)pz <= o.zmax && (double)gz <= o.zmax;
                if (!ok && bad++ < 5) {
                    printf("    box %d point %d: (%g, %g) Z %g outside (%g..%g, %g..%g) Z %g\n", n,
                           k, (double)px, (double)py, 16.0 * (double)pz, o.x0, o.x1, o.y0, o.y1,
                           o.zmax);
                }
            }
        }
    }
    setRound(0);
    CHECK(bad == 0 && seen > 500u, "occlusion: %u of %u points outside their occludee's box", bad,
          pts);
    printf("  occlusion: %u occludee boxes, %u points inside them\n", seen, pts);
}

/* cell (i, j) of the record (tile by tile) */
static uint32_t hizCell(const RdVisHiz *z, uint32_t i, uint32_t j)
{
    const uint32_t t = (j >> 3) * z->ntx + (i >> 3);
    return z->cell[((size_t)t << 6) | ((j & 7u) << 3) | (i & 7u)];
}

/* the least distance of point (x, y) inside the triangle (positive) to its
 * edges; negative outside */
static double triDist(const double tx[3], const double ty[3], double x, double y)
{
    const double a2 = (tx[1] - tx[0]) * (ty[2] - ty[0]) - (ty[1] - ty[0]) * (tx[2] - tx[0]);
    const double s = a2 > 0.0 ? 1.0 : -1.0;
    double d = 1e300;
    for (int e = 0; e < 3; e++) {
        const int n = (e + 1) % 3;
        const double ux = tx[n] - tx[e], uy = ty[n] - ty[e];
        const double f = s * (ux * (y - ty[e]) - uy * (x - tx[e])) / hypot(ux, uy);
        d = f < d ? f : d;
    }
    return d;
}

static void checkHizRaster(void)
{
    static RdVisHiz z;
    const RdVisRect r = rd__vis_rect(512, 448, 512, 448);
    CHECK(rd__vis_hiz_setup(&z, &r), "occlusion: the record set up");
    const double all[4] = {0.0, 0.0, 8192.0, 8192.0};
    uint64_t cells = 0, checked = 0;
    uint32_t bad = 0;
    for (int n = 0; n < 300; n++) {
        rd__vis_hiz_reset(&z);
        RdVisHizTri t;
        memset(&t, 0, sizeof(t));
        for (int k = 0; k < 3; k++) {
            t.x[k] = uni(r.x0 - 20.0, r.x1 + 20.0);
            t.y[k] = uni(r.y0 - 20.0, r.y1 + 20.0);
        }
        t.r = uni(0.0, 0.25);
        t.lb = 1000;
        t.sel = 1;
        const uint32_t got = rd__vis_hiz_raster(&z, &t, all);
        cells += got;
        uint32_t marked = 0;
        /* a row's cells covered: one run (the triangle is convex), its two
         * ends the nearest to the edges (the distance to a line is linear
         * along the row), so they are the ones checked */
        for (uint32_t j = 0; j < z.ncy; j++) {
            int64_t first = -1, last = -1;
            for (uint32_t i = 0; i < z.ncx; i++) {
                if (hizCell(&z, i, j) == 1000u) {
                    marked++;
                    first = first < 0 ? (int64_t)i : first;
                    if (last >= 0 && last != (int64_t)i - 1) {
                        CHECK(false, "occlusion: triangle %d row %u covered in two runs", n, j);
                    }
                    last = (int64_t)i;
                }
            }
            for (int end = 0; end < 2 && first >= 0; end++) {
                const uint32_t i = (uint32_t)(end ? last : first);
                const double X = z.gx0 + 2.0 * i, Y = z.gy0 + 2.0 * j;
                /* the square grown by a pixel each way, half more up and down */
                const double qx[4] = {X - 1.0, X + 3.0, X - 1.0, X + 3.0};
                const double qy[4] = {Y - 1.5, Y - 1.5, Y + 3.5, Y + 3.5};
                /* every corner placement at the extremes, and 16 inside */
                for (int p = 0; p < 64 + 16; p++) {
                    double mx[3], my[3];
                    for (int k = 0; k < 3; k++) {
                        if (p < 64) {
                            mx[k] = t.x[k] + ((p >> (2 * k)) & 1 ? t.r : -t.r);
                            my[k] = t.y[k] + ((p >> (2 * k + 1)) & 1 ? t.r : -t.r);
                        } else {
                            mx[k] = t.x[k] + uni(-t.r, t.r);
                            my[k] = t.y[k] + uni(-t.r, t.r);
                        }
                    }
                    for (int q = 0; q < 4; q++) {
                        checked++;
                        if (!(triDist(mx, my, qx[q], qy[q]) >= 0.25 - 1e-9) && bad++ < 5) {
                            printf("    triangle %d cell (%u, %u): corner %d %g from an edge\n", n,
                                   i, j, q, triDist(mx, my, qx[q], qy[q]));
                        }
                    }
                }
            }
        }
        CHECK(marked == got, "occlusion: triangle %d covered %u cells, %u counted", n, marked, got);
    }
    CHECK(bad == 0 && cells > 1000u, "occlusion: %u of %llu corner tests within 0.25 pixel", bad,
          (unsigned long long)checked);

    /* the inner rule at a cell's corner: a right angle on a cell's corner
     * (r 0): the cells on its two legs not covered (the grown square
     * reaches past them), the next ones in covered; a leg 0.3 pixel left of
     * a cell's grown left edge (a pixel left of the cell) covers it, 0.2
     * does not */
    const uint32_t a = 40, b = 40;
    const double X = z.gx0 + 2.0 * a, Y = z.gy0 + 2.0 * b;
    RdVisHizTri t;
    memset(&t, 0, sizeof(t));
    t.lb = 7;
    t.sel = 1;
    const double xs[3] = {X, X + 300.0, X}, ys[3] = {Y, Y, Y + 300.0};
    memcpy(t.x, xs, sizeof(xs));
    memcpy(t.y, ys, sizeof(ys));
    rd__vis_hiz_reset(&z);
    rd__vis_hiz_raster(&z, &t, all);
    CHECK(hizCell(&z, a + 1, b + 1) == 7u && hizCell(&z, a, b + 1) == 0u &&
              hizCell(&z, a + 1, b) == 0u && hizCell(&z, a, b) == 0u,
          "occlusion: the right angle's cells %u %u %u %u", hizCell(&z, a, b),
          hizCell(&z, a + 1, b), hizCell(&z, a, b + 1), hizCell(&z, a + 1, b + 1));
    for (int k = 0; k < 2; k++) {
        const double in = k ? 0.2 : 0.3;
        const double x0 = X + 2.0 - 1.0 - in;
        const double xx[3] = {x0, x0 + 300.0, x0}, yy[3] = {Y, Y, Y + 300.0};
        memcpy(t.x, xx, sizeof(xx));
        memcpy(t.y, yy, sizeof(yy));
        rd__vis_hiz_reset(&z);
        rd__vis_hiz_raster(&z, &t, all);
        CHECK((hizCell(&z, a + 1, b + 2) == 7u) == (k == 0) && hizCell(&z, a, b + 2) == 0u,
              "occlusion: a leg %g pixel out of the cell's grown edge: %s", in,
              k ? "covered" : "not covered");
    }
    rd__vis_hiz_free(&z);
    printf("  occlusion: %llu cells covered, %llu corner placements inside by 0.25 pixel\n",
           (unsigned long long)cells, (unsigned long long)checked);
}

/* the box lo..hi (w 1) through the orthographic block against the record:
 * hidden (later: with the later mark's room) */
static bool hizHides(RdVisHiz *z, double x0, double y0, double x1, double y1, float zlo, float zhi,
                     bool later, RdVisHizBox *out)
{
    const float lo[4] = {(float)x0, (float)y0, zlo, 1.0f},
                hi[4] = {(float)x1, (float)y1, zhi, 1.0f};
    RdVisBox b;
    boxOf(&b, lo, hi);
    RdVisHizBox o;
    if (!rd__vis_hiz_occludee(kOrtho, &b, false, &o)) {
        return false;
    }
    if (out) {
        *out = o;
    }
    return rd__vis_hiz_hidden(z, &o, o.zmax + rd__vis_hiz_margin(o.zmax, later));
}

/* a wall triangle at depth z (GS Z 16 z) through the orthographic block */
static bool hizWall(RdVisHiz *z, float depth, RdVisHizTri *t)
{
    const float p[3][4] = {{1900, 1900, depth, 1}, {2300, 1900, depth, 1}, {1900, 2200, depth, 1}};
    RdVisHizVtx v[3];
    for (int k = 0; k < 3; k++) {
        if (!rd__vis_hiz_vertex(kOrtho, NULL, p[k], &v[k])) {
            return false;
        }
    }
    if (!rd__vis_hiz_tri(&v[0], &v[1], &v[2], t)) {
        return false;
    }
    const double all[4] = {0.0, 0.0, 8192.0, 8192.0};
    return rd__vis_hiz_raster(z, t, all) > 0u;
}

static void checkHizHidden(void)
{
    static RdVisHiz z;
    const RdVisRect r = rd__vis_rect(512, 448, 512, 448);
    CHECK(rd__vis_hiz_setup(&z, &r), "occlusion: the record set up");
    CHECK(!hizHides(&z, 1950, 1950, 2000, 2000, 400, 500, false, NULL),
          "occlusion: nothing hidden by an empty record");
    RdVisHizTri t;
    CHECK(hizWall(&z, 1000.0f, &t), "occlusion: the wall covers cells");
    CHECK(hizHides(&z, 1950, 1950, 2000, 2000, 400, 500, false, NULL),
          "occlusion: a box behind the wall hidden");
    CHECK(!hizHides(&z, 1950, 1950, 2000, 2000, 1000.0625f, 1000.0625f, false, NULL),
          "occlusion: a box one GS Z unit in front kept");
    CHECK(!hizHides(&z, 1950, 1950, 2000, 2000, 1000.0f, 1000.0f, false, NULL),
          "occlusion: a coplanar box kept");
    CHECK(!hizHides(&z, 1950, 1950, 2000, 2000, 800.0f, 800.0f, false, NULL),
          "occlusion: a box behind by less than the margin kept");
    CHECK(hizHides(&z, 1950, 1950, 2000, 2000, 690.0f, 690.0f, false, NULL) &&
              !hizHides(&z, 1950, 1950, 2000, 2000, 690.0f, 690.0f, true, NULL),
          "occlusion: the later mark's room");
    CHECK(!hizHides(&z, 2140, 2000, 2180, 2040, 400, 500, false, NULL),
          "occlusion: a box reaching past the wall's edge (a part-covered tile) kept");
    CHECK(!hizHides(&z, 1800, 1950, 1850, 2000, 400, 500, false, NULL),
          "occlusion: a box beside the wall kept");
    rd__vis_hiz_reset(&z);
    CHECK(!hizHides(&z, 1950, 1950, 2000, 2000, 400, 500, false, NULL),
          "occlusion: nothing hidden after a reset");

    /* near 2^31: the margin 4096 + 2^31 / 2^17; a box hidden exactly when
     * its bound and the margin stay under the wall's */
    CHECK(rd__vis_hiz_margin(2147483648.0, false) == 20480.0, "occlusion: the margin at 2^31 %g",
          rd__vis_hiz_margin(2147483648.0, false));
    const float zw = 134205728.0f; /* GS Z 2^31 - 200000 - 8192 */
    CHECK(hizWall(&z, zw, &t), "occlusion: a wall near 2^31 covers cells (lb %u)", t.lb);
    uint32_t hid = 0, kept = 0, wrong = 0;
    double gapMin = 1e300;
    for (int k = 0; k < 400; k++) {
        const float zd = zw - 128.0f * (float)k;
        RdVisHizBox o;
        memset(&o, 0, sizeof(o)); /* a box the test refuses: counted wrong below */
        const bool h = hizHides(&z, 1950, 1950, 2000, 2000, zd, zd, false, &o);
        const bool want = o.zmax + rd__vis_hiz_margin(o.zmax, false) < (double)t.lb;
        wrong += h != want;
        hid += h;
        kept += !h;
        if (h) {
            const double gap = (double)t.lb - 16.0 * (double)zd;
            gapMin = gap < gapMin ? gap : gapMin;
        }
    }
    CHECK(wrong == 0 && hid > 0 && kept > 0 && gapMin > 20480.0,
          "occlusion near 2^31: %u hidden, %u kept, %u against the bound, least gap %g", hid, kept,
          wrong, gapMin);
    rd__vis_hiz_free(&z);
    printf("  occlusion: behind hidden; in front, coplanar, inside the margin, the later room, a "
           "part-covered tile kept; near 2^31 the least gap hidden %g\n",
           gapMin);
}

/* ------------------------------------------------------------------- frame */

static uint32_t makeMesh(uint32_t qpv, const float lo[3], const float hi[3], const char *name)
{
    enum { NV = 8 };

    static float st[NV * 5][4];
    memset(st, 0, sizeof(st));
    for (int v = 0; v < NV; v++) {
        float *p = st[v * qpv];
        for (int i = 0; i < 3; i++) {
            p[i] = (v >> i) & 1 ? hi[i] : lo[i];
        }
        p[3] = 1.0f;
        st[v * qpv + qpv - 2][3] = v == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        if (qpv == 5) {
            setAddr(st[v * qpv + 2], 16, 1.0f, 16, 0.0f);
        }
    }
    uint32_t index[(NV - 2) * 3];
    for (int k = 2; k < NV; k++) {
        for (int c = 0; c < 3; c++) {
            index[(k - 2) * 3 + c] = ICO_VU_INDEX(k, c);
        }
    }
    RdVuBatchRec batch;
    memset(&batch, 0, sizeof(batch));
    batch.firstIndex = 0;
    batch.indexCount = (NV - 2) * 3;
    batch.firstVertex = 0;
    batch.vertexCount = NV;
    return rd__vu_mesh_create_raw((const float (*)[4])st, NV, qpv, index, (NV - 2) * 3, &batch, 1,
                                  name);
}

/* a VU command into list l: the payload with mem[16..19] (and mem[4..7]) m,
 * the bones (skinned: one bone, the identity) or the stream (grids) */
static RdCmd *pushVu(RdFrame *f, int l, uint8_t type, uint32_t mesh, uint8_t prog, uint8_t clip,
                     const float (*m)[4], const float (*stream)[4], uint32_t streamQw, uint32_t vpb)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock) + 64 * 16];
    memset(buf, 0, sizeof(buf));
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.code = clip == RD_VU_CLIP_SCISSOR ? 36 : clip == RD_VU_CLIP_NONE ? 34 : 32;
    p.clip = clip;
    p.prog = prog;
    p.batchCount = 1;
    RdVuBlock blk;
    memset(&blk, 0, sizeof(blk));
    memcpy(&blk.mem[16], m, 64);
    memcpy(&blk.mem[4], m, 64);
    uint32_t extra = 0;
    if (type == RDC_SKINNED) {
        p.boneQw = 4;
        memcpy(buf + sizeof(p) + sizeof(blk), kOrtho, 64);
        extra = 64;
    } else if (stream) {
        p.streamQw = streamQw;
        p.vertsPerBatch = vpb;
        p.qwPerVertex = 2;
        p.batchCount = streamQw / (vpb * 2 + 4);
        memcpy(buf + sizeof(p) + sizeof(blk), stream, (size_t)streamQw * 16);
        extra = streamQw * 16;
    }
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &blk, sizeof(blk));
    const uint32_t size = (uint32_t)(sizeof(p) + sizeof(blk)) + extra;
    const uint32_t off = rd__frame_payload(f, buf, size);
    RdCmd *c = rd__st_push(f, l, type);
    c->u[0] = mesh;
    c->b[0] = prog;
    c->u[1] = off;
    c->u[2] = size;
    return c;
}

static void pushTarget(RdFrame *f, int l, uint32_t id)
{
    RdCmd *c = rd__st_push(f, l, RDC_TARGET);
    c->u[0] = id;
    c->u[1] = id;
    c->u[2] = 512u | (448u << 16);
    c->b[0] = 1;
}

static void checkFrame(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    /* positions: in view about (2048, 2048); left of the frame; behind the
     * eye (w = z below 0 under wz) */
    const float inLo[3] = {2000, 2000, 1}, inHi[3] = {2100, 2100, 2};
    const float leftLo[3] = {1000, 2000, 1}, leftHi[3] = {1100, 2100, 2};
    const float backLo[3] = {-5, -5, -20}, backHi[3] = {5, 5, -10};
    const uint32_t mIn = makeMesh(3, inLo, inHi, "vis in");
    const uint32_t mLeft = makeMesh(4, leftLo, leftHi, "vis left");
    const uint32_t mBack = makeMesh(3, backLo, backHi, "vis back");
    const uint32_t mSkin = makeMesh(5, leftLo, leftHi, "vis skin");
    CHECK(mIn && mLeft && mBack && mSkin, "meshes made");
    const float wz[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}};
    /* a grid of two strips of 3 vertices left of the frame */
    float grid[20][4];
    memset(grid, 0, sizeof(grid));
    for (int s = 0; s < 2; s++) {
        for (int k = 0; k < 3; k++) {
            float *p = grid[s * 10 + 3 + k * 2];
            p[0] = 1000.0f + (float)k;
            p[1] = 2048.0f + (float)s;
            p[2] = 1.0f;
            p[3] = 1.0f;
        }
    }
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 7;
    /* list 0: TARGET, then the draws (their indices noted) */
    pushTarget(&f, 0, scene);
    uint32_t idx[16];
    int n = 0;
    pushVu(&f, 0, RDC_MESH, mIn, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 0: kept, in view */
    pushVu(&f, 0, RDC_MESH, mLeft, RD_PROG_LIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 1: skipped, left */
    pushVu(&f, 0, RDC_MESH, mBack, RD_PROG_PRELIT, RD_VU_CLIP_REGION, wz, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 2: skipped, behind */
    pushVu(&f, 0, RDC_MESH, mLeft, RD_PROG_LIT, RD_VU_CLIP_NONE, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 3: kept, code 34 */
    pushVu(&f, 0, RDC_PARTICLES, 0, RD_PROG_PARTICLE, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 4: kept, particles */
    pushVu(&f, 0, RDC_SKINNED, mSkin, RD_PROG_SKIN, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 5: skipped, skinned left */
    pushVu(&f, 0, RDC_GRID, 0, RD_PROG_GRID, RD_VU_CLIP_REGION, kOrtho, (const float (*)[4])grid,
           20, 3);
    idx[n++] = f.lists[0].count - 1; /* 6: skipped, grid left */
    pushVu(&f, 0, RDC_MESH, mBack, RD_PROG_PRELIT, RD_VU_CLIP_SCISSOR, wz, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 7: kept, behind under code 36 */
    pushTarget(&f, 0, 0);
    pushVu(&f, 0, RDC_MESH, mLeft, RD_PROG_LIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    idx[n++] = f.lists[0].count - 1; /* 8: kept, no target */
    /* list 5: one skipped; list 11: the 2D layer's list, kept */
    pushTarget(&f, 5, scene);
    pushVu(&f, 5, RDC_MESH, mLeft, RD_PROG_LIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    const uint32_t idx5 = f.lists[5].count - 1;
    pushTarget(&f, 11, scene);
    pushVu(&f, 11, RDC_MESH, mLeft, RD_PROG_LIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    /* list 3 holds no VU draw: shared, never copied */
    pushTarget(&f, 3, scene);
    f.closed = 1;
    static const bool skipped[9] = {false, true, true, false, false, true, true, false, false};

    rd__vis_set_mode(RD_VIS_FRUSTUM);
    const RdFrame *s = rd__vis_frame(&f, 0);
    CHECK(s != &f, "frame: a shadow made");
    if (s == &f) {
        rd__frame_free(&f);
        return;
    }
    for (int k = 0; k < n; k++) {
        const RdCmd *sc = &s->lists[0].cmds[idx[k]], *oc = &f.lists[0].cmds[idx[k]];
        CHECK((sc->type == RDC_NOP) == skipped[k], "frame: command %d (%u) %s", k, idx[k],
              skipped[k] ? "not skipped" : "skipped");
        CHECK(memcmp(&sc->b, &oc->b, sizeof(RdCmd) - 1) == 0, "frame: command %d's fields", k);
        CHECK((rd__vis_culled(s, 0, idx[k]) == oc) == skipped[k], "frame: rd__vis_culled %d", k);
    }
    for (uint32_t i = 0; i < f.lists[0].count; i++) {
        const RdCmd *sc = &s->lists[0].cmds[i], *oc = &f.lists[0].cmds[i];
        bool culled = false;
        for (int k = 0; k < n; k++) {
            culled = culled || (idx[k] == i && skipped[k]);
        }
        if (!culled) {
            CHECK(memcmp(sc, oc, sizeof(RdCmd)) == 0, "frame: command %u changed", i);
        }
    }
    CHECK(s->lists[0].count == f.lists[0].count && s->lists[0].cmds != f.lists[0].cmds,
          "frame: list 0 copied");
    CHECK(s->lists[5].cmds[idx5].type == RDC_NOP && s->lists[5].cmds != f.lists[5].cmds,
          "frame: list 5's draw skipped");
    CHECK(s->lists[11].cmds == f.lists[11].cmds && s->lists[3].cmds == f.lists[3].cmds,
          "frame: lists 11 and 3 shared");
    CHECK(f.lists[0].cmds[idx[1]].type == RDC_MESH, "frame: the frame itself untouched");
    CHECK(s->payload == f.payload && s->number == f.number && s->keep == f.keep &&
              memcmp(&s->startState, &f.startState, sizeof(f.startState)) == 0,
          "frame: the payload and the head kept");
    CHECK(rd__vis_source(s) == &f && rd__vis_source(&f) == &f, "frame: rd__vis_source");
    CHECK(rd__vis_culled(&f, 0, idx[1]) == NULL, "frame: rd__vis_culled of the frame itself");
    RdVisCounts vc;
    rd__vis_counts(&vc);
    CHECK(vc.draws == 11u && vc.skipped == 5u && vc.frustum == 5u && vc.occluded == 0u,
          "frame: %u of %u skipped (%u frustum)", vc.skipped, vc.draws, vc.frustum);
    CHECK(vc.byVerdict[RD_VIS_LEFT] == 4u && vc.byVerdict[RD_VIS_NEAR] == 1u,
          "frame: left %u, behind %u", vc.byVerdict[RD_VIS_LEFT], vc.byVerdict[RD_VIS_NEAR]);
    CHECK(vc.exempt == 4u, "frame: %u exempt (code 34, particles, no target, list 11)", vc.exempt);
    CHECK(g_rdPerf.cullDraws == 11u && g_rdPerf.cullSkipped == 5u && g_rdPerf.cullFrustum == 5u,
          "frame: the performance record's counts");
    uint32_t why[RD_VIS_WHY_COUNT], whySum = 0;
    rd__vis_why(why);
    for (int k = 0; k < RD_VIS_WHY_COUNT; k++) {
        whySum += why[k];
    }
    CHECK(whySum == vc.draws && why[RD_VIS_WHY_SKIP_WHOLE] == 5u &&
              why[RD_VIS_WHY_SKIP_RANGE] == 0u && why[RD_VIS_WHY_KEEP_MESH_1] == 1u &&
              why[RD_VIS_WHY_KEEP_NONE] == 1u && why[RD_VIS_WHY_PARTICLES] == 1u &&
              why[RD_VIS_WHY_KEEP_SCISSOR] == 1u && why[RD_VIS_WHY_NO_TARGET] == 1u &&
              why[RD_VIS_WHY_LIST_2D] == 1u,
          "frame: the reasons (sum %u of %u): whole %u, one-batch %u, clip none %u, particles "
          "%u, scissor %u, no target %u, 2D %u",
          whySum, vc.draws, why[RD_VIS_WHY_SKIP_WHOLE], why[RD_VIS_WHY_KEEP_MESH_1],
          why[RD_VIS_WHY_KEEP_NONE], why[RD_VIS_WHY_PARTICLES], why[RD_VIS_WHY_KEEP_SCISSOR],
          why[RD_VIS_WHY_NO_TARGET], why[RD_VIS_WHY_LIST_2D]);
    CHECK(rd__vis_why_name(RD_VIS_WHY_SKIP_RANGE)[0] != '?' && rd__vis_why_name(-1)[0] == '?' &&
              rd__vis_why_name(RD_VIS_WHY_COUNT)[0] == '?',
          "frame: the reason names");

    /* a stream written again (a new serial) is bounded again: mLeft's
     * positions moved into view, the lit draws kept */
    RdMeshRec *ml = rd__mesh_rec(mLeft);
    CHECK(ml != NULL, "frame: mLeft's record");
    if (ml) {
        for (uint32_t v = 0; v < ml->vertexCount; v++) {
            ml->stream[(size_t)v * ml->qwPerVertex][0] += 1000.0f;
        }
        rd__mesh_stream_written(ml);
        s = rd__vis_frame(&f, 0);
        CHECK(s != &f && s->lists[0].cmds[idx[1]].type == RDC_MESH &&
                  s->lists[0].cmds[idx[2]].type == RDC_NOP,
              "frame: the moved stream's draw kept, the others as before");
        for (uint32_t v = 0; v < ml->vertexCount; v++) {
            ml->stream[(size_t)v * ml->qwPerVertex][0] -= 1000.0f;
        }
        rd__mesh_stream_written(ml);
    }
    /* the frame itself: mode off, a pass window, number 0 */
    rd__vis_set_mode(RD_VIS_OFF);
    CHECK(rd__vis_frame(&f, 0) == &f && rd__vis_mode() == RD_VIS_OFF, "frame: off");
    rd__vis_set_mode(RD_VIS_FULL);
    s = rd__vis_frame(&f, 0);
    CHECK(s != &f && s->lists[0].cmds[idx[1]].type == RDC_NOP, "frame: full skips as frustum");
    rd__vis_set_mode(RD_VIS_FRUSTUM);
    g_rdWin.first = 1;
    CHECK(rd__vis_frame(&f, 0) == &f, "frame: a pass window");
    g_rdWin.first = 0;
    f.number = 0;
    CHECK(rd__vis_frame(&f, 0) == &f, "frame: number 0");
    f.number = 7;
    /* keep: lists 11 and 12 alone, whose draws are never tested */
    CHECK(rd__vis_frame(&f, 1) == &f, "frame: a keep frame skips nothing");
    rd__vis_set_mode(-1);
    rd__frame_free(&f);
    printf("  frame: 5 of 11 VU commands skipped into the shadow, the frame kept\n");
}

/* an environment switch to v (NULL: unset) */
static void setEnvVar(const char *name, const char *v)
{
#ifdef _WIN32
    _putenv_s(name, v ? v : "");
#else
    if (v) {
        setenv(name, v, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/* a wall of two triangles (a strip of four prelit vertices) at depth z,
 * its vertices' colour alpha a */
static uint32_t makeWall(float x0, float y0, float x1, float y1, float z, float a, const char *name)
{
    float st[4 * 3][4];
    memset(st, 0, sizeof(st));
    const float xy[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    for (int v = 0; v < 4; v++) {
        float *p = st[v * 3];
        p[0] = xy[v][0];
        p[1] = xy[v][1];
        p[2] = z;
        p[3] = 1.0f;
        st[v * 3 + 1][3] = v == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        st[v * 3 + 2][3] = a;                    /* the colour's alpha */
    }
    const uint32_t index[6] = {ICO_VU_INDEX(2, 0), ICO_VU_INDEX(2, 1), ICO_VU_INDEX(2, 2),
                               ICO_VU_INDEX(3, 0), ICO_VU_INDEX(3, 1), ICO_VU_INDEX(3, 2)};
    RdVuBatchRec batch;
    memset(&batch, 0, sizeof(batch));
    batch.indexCount = 6;
    batch.vertexCount = 4;
    return rd__vu_mesh_create_raw((const float (*)[4])st, 4, 3, index, 6, &batch, 1, name);
}

static void pushBlend(RdFrame *f, uint8_t abe)
{
    RdCmd *c = rd__st_push(f, 0, RDC_BLEND);
    c->b[0] = RD_BLEND_LERP_AS;
    c->b[1] = 0x80;
    c->b[2] = abe;
}

static void pushClear(RdFrame *f, uint32_t id)
{
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    c->u[0] = id;
    c->b[4] = 1;
}

/* the occlusion test on a recorded frame: list 0 a sequence of epochs,
 * each a clear, a wall under some state and a box behind it */
static void checkHizFrame(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t wall = makeWall(1800, 1830, 2300, 2260, 1000.0f, 0.0f, "hiz wall");
    const uint32_t wallA = makeWall(1800, 1830, 2300, 2260, 1000.0f, 127.0f, "hiz wall alpha");
    const float bLo[3] = {1850, 1870, 400}, bHi[3] = {1900, 1920, 500};
    const float fLo[3] = {1850, 1870, 1100}, fHi[3] = {1900, 1920, 1200};
    const uint32_t behind = makeMesh(3, bLo, bHi, "hiz behind");
    const uint32_t front = makeMesh(3, fLo, fHi, "hiz front");
    CHECK(wall && wallA && behind && front, "occlusion frame: meshes made");
    /* the alpha plan's range of As (rd_alpha_plan.c), on as on every device
     * class */
    const bool alphaPlan = g_rd.profile.alphaPlan;
    g_rd.profile.alphaPlan = true;
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 9;
    pushTarget(&f, 0, scene);
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);

    enum { NB = 12 };

    uint32_t box[NB];
    bool hidden[NB];
    uint32_t walls[NB];
    int nb = 0, nw = 0;
#define WALL()                                                                                     \
    do {                                                                                           \
        pushVu(&f, 0, RDC_MESH, wall, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);      \
        walls[nw++] = f.lists[0].count - 1;                                                        \
    } while (0)
#define BOX(mesh, hid)                                                                             \
    do {                                                                                           \
        pushVu(&f, 0, RDC_MESH, mesh, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);      \
        box[nb] = f.lists[0].count - 1;                                                            \
        hidden[nb++] = hid;                                                                        \
    } while (0)
    /* 0, 1: an opaque wall: behind hidden, in front kept */
    WALL();
    BOX(behind, true);
    BOX(front, false);
    /* 2: a wall under the alpha test (GREATER 0) whose As may be 0 */
    pushClear(&f, scene);
    rd__st_test(&f, 0, RD_TEST_AT_GT0_Z_GEQUAL);
    WALL();
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    BOX(behind, false);
    /* 3: a blended wall */
    pushClear(&f, scene);
    pushBlend(&f, 1);
    WALL();
    pushBlend(&f, 0);
    BOX(behind, false);
    /* 4: a wall without Z write */
    pushClear(&f, scene);
    rd__st_push1(&f, 0, RDC_ZWRITE, RD_ZWRITE_OFF);
    WALL();
    rd__st_push1(&f, 0, RDC_ZWRITE, RD_ZWRITE_ON);
    BOX(behind, false);
    /* 5: the box under its own Z test ALWAYS */
    pushClear(&f, scene);
    WALL();
    rd__st_test(&f, 0, RD_TEST_Z_ALWAYS);
    BOX(behind, false);
    /* 6: a wall under Z test ALWAYS (every triangle exact) hides */
    pushClear(&f, scene);
    WALL();
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    BOX(behind, true);
    /* 7: a draw that may lower the depth between them (particles under Z
     * test ALWAYS with Z write) */
    pushClear(&f, scene);
    WALL();
    rd__st_test(&f, 0, RD_TEST_Z_ALWAYS);
    pushVu(&f, 0, RDC_PARTICLES, 0, RD_PROG_PARTICLE, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    BOX(behind, false);
    /* 8: a clear between them */
    pushClear(&f, scene);
    WALL();
    pushClear(&f, scene);
    BOX(behind, false);
    /* 9: a wall with the chain's clip plane */
    pushClear(&f, scene);
    RdCmd *cp =
        pushVu(&f, 0, RDC_MESH, wall, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    cp->b[5] = 1;
    walls[nw++] = f.lists[0].count - 1;
    BOX(behind, false);
    /* 10: a wall under the alpha test (GREATER 0x40, AFAIL FB_ONLY, as the
     * scene's meshes) whose As is 127 at every pixel: an occluder */
    pushClear(&f, scene);
    rd__st_test(&f, 0, RD_TEST_AT_GT64_FBONLY);
    pushVu(&f, 0, RDC_MESH, wallA, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    walls[nw++] = f.lists[0].count - 1;
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    BOX(behind, true);
    /* 11: the same box in list 5 (list 0 alone is tested) */
    pushTarget(&f, 5, scene);
    rd__st_test(&f, 5, RD_TEST_Z_GEQUAL);
    pushVu(&f, 5, RDC_MESH, behind, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    const uint32_t box5 = f.lists[5].count - 1;
    f.closed = 1;
#undef WALL
#undef BOX
    /* the walls' Z test: the state ahead of each, ALWAYS where case 6 set
     * it (rd__st_test(GEQUAL) after the ALWAYS wall) */

    rd__vis_set_mode(RD_VIS_FULL);
    rd__vis_init();
    const RdFrame *s = rd__vis_frame(&f, 0);
    CHECK(s != &f, "occlusion frame: a shadow made");
    RdVisCounts vc;
    rd__vis_counts(&vc);
    if (s != &f) {
        for (int k = 0; k < nb; k++) {
            CHECK((s->lists[0].cmds[box[k]].type == RDC_NOP) == hidden[k],
                  "occlusion frame: box %d (command %u) %s", k, box[k],
                  hidden[k] ? "not hidden" : "hidden");
        }
        for (int k = 0; k < nw; k++) {
            CHECK(s->lists[0].cmds[walls[k]].type == RDC_MESH, "occlusion frame: wall %d drawn", k);
        }
        CHECK(s->lists[5].cmds == f.lists[5].cmds && f.lists[5].cmds[box5].type == RDC_MESH,
              "occlusion frame: list 5's box kept");
    }
    CHECK(vc.occluded == 3u && vc.frustum == 0u && vc.byVerdict[RD_VIS_OCCLUDED] == 3u &&
              g_rdPerf.cullOccluded == 3u,
          "occlusion frame: %u occluded, %u frustum", vc.occluded, vc.frustum);
    CHECK(vc.hizTris > 0u && vc.hizCells > 0u && vc.hizTested == 4u && vc.hizResets == 5u,
          "occlusion frame: %u triangles, %llu cells, %u tested, %u resets", vc.hizTris,
          (unsigned long long)vc.hizCells, vc.hizTested, vc.hizResets);
    printf("  occlusion frame: %u of %d boxes hidden, %u occluder triangles, %llu cells, %u "
           "tested, %u resets\n",
           vc.occluded, nb, vc.hizTris, (unsigned long long)vc.hizCells, vc.hizTested,
           vc.hizResets);

    /* frustum alone: nothing occluded */
    rd__vis_set_mode(RD_VIS_FRUSTUM);
    s = rd__vis_frame(&f, 0);
    rd__vis_counts(&vc);
    CHECK(s == &f && vc.occluded == 0u, "occlusion frame: frustum hides nothing here");

    /* the budget: none, then one triangle (the first wall's first, the
     * largest box first and the earlier at a tie) */
    rd__vis_set_mode(RD_VIS_FULL);
    setEnvVar("ICO_RD_CULL_HIZ_TRIS", "0");
    rd__vis_init();
    s = rd__vis_frame(&f, 0);
    rd__vis_counts(&vc);
    CHECK(s == &f && vc.occluded == 0u && vc.hizTris == 0u, "occlusion frame: budget 0");
    setEnvVar("ICO_RD_CULL_HIZ_TRIS", "1");
    rd__vis_init();
    s = rd__vis_frame(&f, 0);
    rd__vis_counts(&vc);
    CHECK(s != &f && vc.occluded == 1u && vc.hizTris == 1u && vc.hizOverBudget > 0u &&
              s->lists[0].cmds[box[0]].type == RDC_NOP && s->lists[0].cmds[box[6]].type == RDC_MESH,
          "occlusion frame: budget 1: %u occluded, %u rasterised, %u past it", vc.occluded,
          vc.hizTris, vc.hizOverBudget);
    setEnvVar("ICO_RD_CULL_HIZ_TRIS", NULL);
    /* ICO_VK_DEPTH_ALWAYS: every depth test passes, nothing hidden */
    setEnvVar("ICO_VK_DEPTH_ALWAYS", "1");
    rd__vis_init();
    s = rd__vis_frame(&f, 0);
    rd__vis_counts(&vc);
    CHECK(vc.occluded == 0u, "occlusion frame: ICO_VK_DEPTH_ALWAYS hides nothing");
    setEnvVar("ICO_VK_DEPTH_ALWAYS", NULL);
    rd__vis_set_mode(-1);
    rd__vis_init();
    g_rd.profile.alphaPlan = alphaPlan;
    rd__frame_free(&f);
}

/* A mesh of two batches (the first in view, the second left of the frame)
 * drawn by batch ranges: the stream's box reaches the frame, the second
 * batch's own box does not, so a draw of it alone is skipped (SKIP_RANGE),
 * a draw of the first or of both is kept; a stream written again is
 * measured again. */
static void checkRange(void)
{
    enum { NV = 8, QPV = 3 };

    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    static float st[NV * QPV][4];
    memset(st, 0, sizeof(st));
    for (int v = 0; v < NV; v++) {
        float *p = st[v * QPV];
        p[0] = (v < 4 ? 2000.0f : 1000.0f) + (float)(v & 1) * 50.0f;
        p[1] = 2000.0f + (float)((v >> 1) & 1) * 50.0f;
        p[2] = 1.0f + (float)(v & 3);
        p[3] = 1.0f;
        st[v * QPV + QPV - 2][3] = v == 0 || v == 4 ? 0.0f : 1.0f;
    }
    uint32_t index[4 * 3];
    for (int b = 0; b < 2; b++) {
        for (int k = 0; k < 2; k++) {
            for (int c = 0; c < 3; c++) {
                index[(b * 2 + k) * 3 + c] = ICO_VU_INDEX(b * 4 + 2 + k, c);
            }
        }
    }
    RdVuBatchRec br[2];
    memset(br, 0, sizeof(br));
    for (int b = 0; b < 2; b++) {
        br[b].firstIndex = (uint32_t)b * 6u;
        br[b].indexCount = 6;
        br[b].firstVertex = (uint32_t)b * 4u;
        br[b].vertexCount = 4;
    }
    const uint32_t mesh = rd__vu_mesh_create_raw((const float (*)[4])st, NV, QPV, index, 12, br, 2,
                                                 "vis two batches");
    CHECK(mesh != 0, "range: the mesh made");
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 9;
    pushTarget(&f, 0, scene);
    static const uint32_t ranges[4][2] = {{0, 1}, {1, 1}, {0, 2}, {1, 0}};
    uint32_t at[4];
    for (int k = 0; k < 4; k++) {
        const RdCmd *c =
            pushVu(&f, 0, RDC_MESH, mesh, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
        at[k] = f.lists[0].count - 1;
        RdVuPayload p;
        memcpy(&p, f.payload + c->u[1], sizeof(p));
        p.firstBatch = ranges[k][0];
        p.batchCount = ranges[k][1];
        memcpy(f.payload + c->u[1], &p, sizeof(p));
    }
    f.closed = 1;
    static const bool skipped[4] = {false, true, false, true};
    rd__vis_set_mode(RD_VIS_FRUSTUM);
    const RdFrame *s = rd__vis_frame(&f, 0);
    CHECK(s != &f, "range: a shadow made");
    if (s != &f) {
        for (int k = 0; k < 4; k++) {
            CHECK((s->lists[0].cmds[at[k]].type == RDC_NOP) == skipped[k],
                  "range: draw %d of batches %u + %u %s", k, ranges[k][0], ranges[k][1],
                  skipped[k] ? "not skipped" : "skipped");
        }
    }
    uint32_t why[RD_VIS_WHY_COUNT];
    rd__vis_why(why);
    CHECK(why[RD_VIS_WHY_SKIP_RANGE] == 2u && why[RD_VIS_WHY_SKIP_WHOLE] == 0u &&
              why[RD_VIS_WHY_KEEP_MESH_1] == 1u && why[RD_VIS_WHY_KEEP_MESH_N] == 1u,
          "range: reasons: range %u, whole %u, one batch %u, batches %u",
          why[RD_VIS_WHY_SKIP_RANGE], why[RD_VIS_WHY_SKIP_WHOLE], why[RD_VIS_WHY_KEEP_MESH_1],
          why[RD_VIS_WHY_KEEP_MESH_N]);
    /* the second batch moved into view: the per-batch boxes are made again */
    RdMeshRec *m = rd__mesh_rec(mesh);
    CHECK(m != NULL, "range: the record");
    if (m) {
        for (uint32_t v = 4; v < NV; v++) {
            m->stream[(size_t)v * QPV][0] += 1000.0f;
        }
        rd__mesh_stream_written(m);
        s = rd__vis_frame(&f, 0);
        CHECK(s == &f, "range: the moved batch's draws kept");
    }
    rd__vis_set_mode(-1);
    rd__frame_free(&f);
    printf("  range: 2 of 4 draws of a two-batch mesh skipped by their batch's box\n");
}

/* Code 34 (clip none) draws are never skipped by the frustum test but take
 * part in the occlusion test: a wall under code 34 hides a box behind it,
 * a box under code 34 is hidden behind a wall, one in front is kept, and
 * the breakdown names each outcome. */
static void checkHizNone(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t wall = makeWall(1800, 1830, 2300, 2260, 1000.0f, 0.0f, "hiz wall34");
    const float bLo[3] = {1850, 1870, 400}, bHi[3] = {1900, 1920, 500};
    const float fLo[3] = {1850, 1870, 1100}, fHi[3] = {1900, 1920, 1200};
    const uint32_t behind = makeMesh(3, bLo, bHi, "hiz behind34");
    const uint32_t front = makeMesh(3, fLo, fHi, "hiz front34");
    CHECK(wall && behind && front, "code 34: meshes made");
    const bool alphaPlan = g_rd.profile.alphaPlan;
    g_rd.profile.alphaPlan = true;
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 9;
    pushTarget(&f, 0, scene);
    rd__st_test(&f, 0, RD_TEST_Z_GEQUAL);
    uint32_t at[3];
    pushVu(&f, 0, RDC_MESH, wall, RD_PROG_PRELIT, RD_VU_CLIP_NONE, kOrtho, NULL, 0, 0);
    pushVu(&f, 0, RDC_MESH, behind, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    at[0] = f.lists[0].count - 1; /* hidden behind the code-34 wall */
    pushClear(&f, scene);
    pushVu(&f, 0, RDC_MESH, wall, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
    pushVu(&f, 0, RDC_MESH, behind, RD_PROG_PRELIT, RD_VU_CLIP_NONE, kOrtho, NULL, 0, 0);
    at[1] = f.lists[0].count - 1; /* a code-34 box hidden behind the wall */
    pushVu(&f, 0, RDC_MESH, front, RD_PROG_PRELIT, RD_VU_CLIP_NONE, kOrtho, NULL, 0, 0);
    at[2] = f.lists[0].count - 1; /* in front: kept */
    f.closed = 1;
    rd__vis_set_mode(RD_VIS_FULL);
    rd__vis_init();
    const RdFrame *s = rd__vis_frame(&f, 0);
    CHECK(s != &f, "code 34: a shadow made");
    if (s != &f) {
        CHECK(s->lists[0].cmds[at[0]].type == RDC_NOP, "code 34: the box behind a code-34 wall");
        CHECK(s->lists[0].cmds[at[1]].type == RDC_NOP, "code 34: a code-34 box behind the wall");
        CHECK(s->lists[0].cmds[at[2]].type == RDC_MESH, "code 34: the box in front kept");
    }
    uint32_t hz[RD_VIS_HZ_COUNT], why[RD_VIS_WHY_COUNT];
    rd__vis_hiz_why(hz);
    rd__vis_why(why);
    CHECK(hz[RD_VIS_HZ_OCC_USED] == 2u && hz[RD_VIS_HZ_HIDDEN] == 2u && hz[RD_VIS_HZ_MARGIN] == 1u,
          "code 34: occluders used %u, hidden %u, within the margin %u", hz[RD_VIS_HZ_OCC_USED],
          hz[RD_VIS_HZ_HIDDEN], hz[RD_VIS_HZ_MARGIN]);
    CHECK(why[RD_VIS_WHY_OCCLUDED] == 2u && why[RD_VIS_WHY_KEEP_NONE] == 2u,
          "code 34: reasons: occluded %u, code 34 reaches %u", why[RD_VIS_WHY_OCCLUDED],
          why[RD_VIS_WHY_KEEP_NONE]);
    /* the frustum alone never skips a code-34 draw */
    rd__vis_set_mode(RD_VIS_FRUSTUM);
    CHECK(rd__vis_frame(&f, 0) == &f, "code 34: the frustum test skips none");
    rd__vis_set_mode(-1);
    rd__vis_init();
    g_rd.profile.alphaPlan = alphaPlan;
    rd__frame_free(&f);
}

/* an occluder whose texture is not uploaded yet (dirty: every texture of
 * a dump's first replay, as the walk runs before the replay's uploads):
 * the alpha proof from the scan the upload will make (rd_vis_hiz.c
 * rd__vis_hiz_pending_scan) */
static void checkHizPendingTexture(void)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    uint8_t opaque[4 * 4 * 4], cut[4 * 4 * 4];
    for (int k = 0; k < 16; k++) {
        const uint8_t px[4] = {200, 180, 160, 0x80};
        memcpy(&opaque[k * 4], px, 4);
        memcpy(&cut[k * 4], px, 4);
        cut[k * 4 + 3] = k == 5 ? 0x40 : 0x80; /* one texel below the reference */
    }
    const RdTex tOpaque = rd_create_texture(4, 4, opaque, RD_TEXA_80_80, "hiz opaque");
    const RdTex tCut = rd_create_texture(4, 4, cut, RD_TEXA_80_80, "hiz cut");
    RdTexAlphaScan scan;
    CHECK(tOpaque.id && rd__vis_hiz_pending_scan(tOpaque.id, &scan) && scan.aLo == 0x80 &&
              scan.aHi == 0x80,
          "pending texture: the scan the upload will make (%u..%u)", scan.aLo, scan.aHi);
    CHECK(tCut.id && rd__vis_hiz_pending_scan(tCut.id, &scan) && scan.aLo == 0x40,
          "pending texture: the cut-out's least alpha %u", scan.aLo);
    CHECK(!rd__vis_hiz_pending_scan(0, &scan), "pending texture: no texture");
    const uint32_t wallA = makeWall(1800, 1830, 2300, 2260, 1000.0f, 127.0f, "hiz wall textured");
    const float bLo[3] = {1850, 1870, 400}, bHi[3] = {1900, 1920, 500};
    const uint32_t behind = makeMesh(3, bLo, bHi, "hiz behind textured");
    static RdFrame f;
    memset(&f, 0, sizeof(f));
    rd__reset_state_block(&f.startState);
    f.number = 11;
    pushTarget(&f, 0, scene);
    uint32_t box[2];
    for (int k = 0; k < 2; k++) {
        pushClear(&f, scene);
        /* the scene's meshes: ATE GREATER 0x60, AFAIL FB_ONLY, Z GEQUAL */
        rd__st_test(&f, 0, 0x5160Du);
        RdCmd *tc = rd__st_push(&f, 0, RDC_TEXTURE);
        tc->u[0] = k == 0 ? tOpaque.id : tCut.id;
        tc->b[0] = RD_TEXFN_MODULATE;
        tc->b[1] = RD_TCC_RGBA;
        pushVu(&f, 0, RDC_MESH, wallA, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
        rd__st_push(&f, 0, RDC_TEXTURE_OFF);
        pushVu(&f, 0, RDC_MESH, behind, RD_PROG_PRELIT, RD_VU_CLIP_REGION, kOrtho, NULL, 0, 0);
        box[k] = f.lists[0].count - 1;
    }
    f.closed = 1;
    const bool alphaPlan = g_rd.profile.alphaPlan;
    g_rd.profile.alphaPlan = true;
    rd__vis_set_mode(RD_VIS_FULL);
    rd__vis_init();
    const RdFrame *s = rd__vis_frame(&f, 0);
    RdVisCounts vc;
    rd__vis_counts(&vc);
    /* As = 0x80 x 127 >> 7 = 127 > 0x60 at every pixel: an occluder; the
     * cut-out's 0x40 x 127 >> 7 = 63 fails: none */
    CHECK(s != &f && s->lists[0].cmds[box[0]].type == RDC_NOP &&
              s->lists[0].cmds[box[1]].type == RDC_MESH && vc.occluded == 1u,
          "pending texture: the box behind the opaque wall hidden, behind the cut-out kept "
          "(%u occluded)",
          vc.occluded);
    rd__vis_set_mode(-1);
    rd__vis_init();
    g_rd.profile.alphaPlan = alphaPlan;
    rd__frame_free(&f);
    printf("  occlusion: a wall whose texture is not uploaded yet proven opaque by the scan the "
           "upload will make\n");
}

int main(void)
{
    checkBounds();
    checkMargins();
    checkPlanes();
    checkSkinned();
    checkOracle();
    checkHizVertex();
    checkHizOccludee();
    checkHizRaster();
    checkHizHidden();
    if (!rd__init_record_only(512, 448)) {
        printf("rd_vis_test: no context\n");
        return 1;
    }
    checkFrame();
    checkRange();
    checkHizFrame();
    checkHizPendingTexture();
    checkHizNone();
    rd_shutdown();
    return ico_check_summary("rd_vis_test");
}

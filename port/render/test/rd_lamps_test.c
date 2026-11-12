/* rd_lamps_test.c: the lamps' glow for less, its pure parts, no device
 * (port/render/rd_lamps.c, rd_lamps.h; docs/port/RENDER_POST.md, "The lamps'
 * glow for less").
 *
 *   rd_lamps_test
 *
 *   t1  rd__lamps_parse: 0, 1, all, off, area, pad, area,pad, pad+area, lowres, unset,
 *       a word it does not know
 *   t2  rd__lamps_default_bits: the band everywhere; the area on the PowerVR
 *       class and a desktop class under Vulkan with the pass area, never on
 *       another class (the Arm, Qualcomm and Apple ones: 0), a tile-based
 *       phone, OpenGL or D3D, or without the pass area
 *   t3  rd__lamps_union, rd__lamps_tile_align: an empty rect adds nothing;
 *       the 32 x 32 tiles round a rect, clamped to the target
 *   t4  rd__lamps_box_texels: every texel whose centre lies in a GS box is in
 *       the rect it gives, on the boot corpus's 512 x 512 frame at the phone's
 *       2364 x 1080 (4.617 x 2.109 texels a GS pixel) and on the phone's own
 *       848 x 448 frame under Full pixel's crop (2366 texels, 3.73 GS columns
 *       cut a side); a box off the target gives none; a box past its edge is
 *       clipped
 *   t5  the chain a lamp's box is made by: rd__vis_hiz_occludee's samples'
 *       box of a stream's bound through a matrix (an orthographic one and a
 *       perspective one), then rd__lamps_box_texels: the texel of every
 *       vertex of the stream (and of every point between two of them) is in
 *       the rect; a corner behind the eye refuses the box
 *   t7  rd__lamps_vertex_box (the lamps' box since round 2): a two-vertex
 *       stream along a diagonal whose bound has a corner behind the eye (the
 *       corners' box, rd__vis_hiz_occludee, refuses it) gives a box holding
 *       the texel of every point between its vertices; a stream with a vertex
 *       behind the eye refused (BEHIND, the count); a matrix lane not
 *       finite refused (FINITE)
 *   t8  rd__lamps_fold4_axis on the chain's first two pairs at 4:3 (list
 *       7:114, 7:124, 7:134, 7:144 of the boot corpus's rd-00400: WORK0 256 x
 *       128 at origin (30720, 31744), WORK1 256 x 256): windows 4 and 5 on x,
 *       4 and 5 on y (a Python mirror of the shader's index arithmetic gave
 *       them); a drift too steep for the windows refused; a divisor 0 refused
 *   t6  rd__lamps_area_pick: the region unknown (FIRST), nothing lit and no
 *       lamp (EMPTY), an area holding WORK2 but a tile (LARGE), else the
 *       tiles round the lamps' box and the lit region (both inside it) */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_lamps.h"
#include "rd_vis.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static bool inRect(const RhiRect *r, int32_t x, int32_t y)
{
    return x >= r->x && y >= r->y && x < r->x + (int32_t)r->w && y < r->y + (int32_t)r->h;
}

/* the texel whose centre is GS point (u, v) of a target with gsW x gsH at
 * the frame's origin (rd_internal.h rd__gs_texel_x) */
static void texelOf(const RdTargetRec *t, uint32_t gsW, uint32_t gsH, double u, double v,
                    int32_t *tx, int32_t *ty)
{
    const double ox = 2048.0 - (double)(gsW >> 1), oy = 2048.0 - (double)(gsH >> 1);
    *tx = (int32_t)floor((u - ox) * (double)t->sx - (double)t->ox);
    *ty = (int32_t)floor((v - oy) * (double)t->sy);
}

static RdTargetRec target(uint32_t w, uint32_t h, uint32_t tw, uint32_t th, float sx, float sy,
                          float ox)
{
    RdTargetRec t;
    memset(&t, 0, sizeof(t));
    t.w = w;
    t.h = h;
    t.tw = tw;
    t.th = th;
    t.sx = sx;
    t.sy = sy;
    t.ox = ox;
    return t;
}

/* every texel centre of the box's GS points (a grid of steps a side) in
 * the rect, where it is on the target */
static void boxHolds(const char *what, const RdTargetRec *t, uint32_t gsW, uint32_t gsH, double x0,
                     double y0, double x1, double y1)
{
    RhiRect r;
    const bool ok = rd__lamps_box_texels(t, gsW, gsH, x0, y0, x1, y1, 2, &r);
    CHECK(ok, "%s: no rect", what);
    if (!ok) {
        return;
    }
    int bad = 0;
    for (int i = 0; i <= 64; i++) {
        for (int j = 0; j <= 64; j++) {
            const double u = x0 + (x1 - x0) * i / 64.0, v = y0 + (y1 - y0) * j / 64.0;
            int32_t tx, ty;
            texelOf(t, gsW, gsH, u, v, &tx, &ty);
            if (tx < 0 || ty < 0 || tx >= (int32_t)t->tw || ty >= (int32_t)t->th) {
                continue; /* off the target */
            }
            bad += !inRect(&r, tx, ty);
        }
    }
    CHECK(bad == 0, "%s: %d texel centres outside %ux%u at %d,%d", what, bad, r.w, r.h, r.x, r.y);
}

/* a stream's corners through m, the box from the occludee bound and the
 * texels: every vertex's texel (and those between two vertices) inside */
static void chainHolds(const char *what, const float m[4][4], const float (*pts)[4], uint32_t n,
                       const RdTargetRec *t, uint32_t gsW, uint32_t gsH)
{
    RdVisBox b;
    CHECK(rd__vis_box(pts, n, 1, &b) && b.n == n, "%s: the bound", what);
    RdVisHizBox hb;
    const bool ok = rd__vis_hiz_occludee(m, &b, false, &hb);
    CHECK(ok, "%s: no samples' box", what);
    if (!ok) {
        return;
    }
    RhiRect r;
    CHECK(rd__lamps_box_texels(t, gsW, gsH, hb.x0, hb.y0, hb.x1, hb.y1, 2, &r), "%s: no rect",
          what);
    int bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            for (int k = 0; k <= 8; k++) {
                double p[4], h[4];
                for (int c = 0; c < 4; c++) {
                    p[c] = pts[i][c] + (pts[j][c] - pts[i][c]) * k / 8.0;
                }
                p[3] = 1.0;
                for (int c = 0; c < 4; c++) {
                    h[c] = m[0][c] * p[0] + m[1][c] * p[1] + m[2][c] * p[2] + m[3][c] * p[3];
                }
                const double u = h[0] / h[3], v = h[1] / h[3];
                int32_t tx, ty;
                texelOf(t, gsW, gsH, u, v, &tx, &ty);
                if (tx < 0 || ty < 0 || tx >= (int32_t)t->tw || ty >= (int32_t)t->th) {
                    continue;
                }
                bad += !inRect(&r, tx, ty);
            }
        }
    }
    CHECK(bad == 0, "%s: %d points outside %ux%u at %d,%d", what, bad, r.w, r.h, r.x, r.y);
}

int main(void)
{
    printf("rd_lamps_test\n");
    uint32_t b = 77;
    /* t1 */
    CHECK(rd__lamps_parse(NULL, &b) == 0 && b == 77, "unset");
    CHECK(rd__lamps_parse("", &b) == 0 && b == 77, "empty");
    CHECK(rd__lamps_parse("0", &b) == 1 && b == 0, "0: %u", b);
    CHECK(rd__lamps_parse("off", &b) == 1 && b == 0, "off: %u", b);
    CHECK(rd__lamps_parse("1", &b) == 1 && b == RD_LAMPS_ALL, "1: %u", b);
    CHECK(rd__lamps_parse("all", &b) == 1 && b == RD_LAMPS_ALL, "all: %u", b);
    CHECK(rd__lamps_parse("area", &b) == 1 && b == RD_LAMPS_AREA, "area: %u", b);
    CHECK(rd__lamps_parse("pad", &b) == 1 && b == RD_LAMPS_PAD, "pad: %u", b);
    CHECK(rd__lamps_parse("area,pad", &b) == 1 && b == (RD_LAMPS_AREA | RD_LAMPS_PAD),
          "area,pad: %u", b);
    CHECK(rd__lamps_parse("pad+area", &b) == 1 && b == (RD_LAMPS_AREA | RD_LAMPS_PAD),
          "pad+area: %u", b);
    CHECK(rd__lamps_parse("owed", &b) == 1 && b == RD_LAMPS_OWED, "owed: %u", b);
    CHECK(rd__lamps_parse("wholepad,fold4", &b) == 1 && b == (RD_LAMPS_WHOLEPAD | RD_LAMPS_FOLD4),
          "wholepad,fold4: %u", b);
    /* LOWRES: only its word asks for it (not in 1 or all: not byte-identical) */
    CHECK(rd__lamps_parse("pad+owed+wholepad+lowres", &b) == 1 &&
              b == (RD_LAMPS_PAD | RD_LAMPS_OWED | RD_LAMPS_WHOLEPAD | RD_LAMPS_LOWRES),
          "pad+owed+wholepad+lowres: %u", b);
    CHECK((RD_LAMPS_ALL & (RD_LAMPS_LOWRES | RD_LAMPS_FILLHOLD)) == 0u,
          "lowres and fillhold outside RD_LAMPS_ALL");
    CHECK(rd__lamps_parse("lowres,fillhold", &b) == 1 && b == (RD_LAMPS_LOWRES | RD_LAMPS_FILLHOLD),
          "lowres,fillhold: %u", b);
    CHECK(rd__lamps_parse("lowresbox", &b) == 1 && b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESBOX),
          "lowresbox implies lowres: %u", b);
    CHECK(rd__lamps_parse("lowresbias+lowres4+lowreshy", &b) == 1 &&
              b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESBIAS | RD_LAMPS_LOWRES4 | RD_LAMPS_LOWRESHY),
          "lowresbias, lowres4, lowreshy imply lowres: %u", b);
    CHECK(rd__lamps_parse("lowresp", &b) == 1 && b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESP),
          "lowresp implies lowres: %u", b);
    CHECK(rd__lamps_parse("lowres2x", &b) == 1 && b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRES2X),
          "lowres2x implies lowres: %u", b);
    CHECK(rd__lamps_parse("lowresms", &b) == 1 && b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESMS),
          "lowresms implies lowres: %u", b);
    CHECK(rd__lamps_parse("lowresms8", &b) == 1 &&
              b == (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESMS | RD_LAMPS_LOWRESMS8),
          "lowresms8 implies lowresms: %u", b);
    CHECK(strcmp(rd__lamps_bits_text(RD_LAMPS_PAD | RD_LAMPS_LOWRES), "pad+lowres") == 0,
          "lowres's word: %s", rd__lamps_bits_text(RD_LAMPS_PAD | RD_LAMPS_LOWRES));
    b = 77;
    CHECK(rd__lamps_parse("lamps", &b) == -1 && b == 77, "an unknown word");
    CHECK(rd__lamps_parse("area,x", &b) == -1 && b == 77, "an unknown second word");
    CHECK(strcmp(rd__lamps_bits_text(RD_LAMPS_ALL), "area+pad+owed+wholepad+fold4") == 0 &&
              strcmp(rd__lamps_bits_text(RD_LAMPS_AREA | RD_LAMPS_PAD), "area+pad") == 0 &&
              strcmp(rd__lamps_bits_text(0), "off") == 0,
          "the bits' words");
    /* t2 */
    /* every class (FILLHOLD by its word alone) */
    const uint32_t ev = RD_LAMPS_PAD | RD_LAMPS_OWED | RD_LAMPS_WHOLEPAD |
                        (RD_LAMPS_LOWRES_DEFAULT ? RD_LAMPS_LOWRES : 0u);
    /* the 4-sample lamps on the PowerVR class under Vulkan alone */
    const uint32_t pv = (RD_LAMPS_MS_DEFAULT ? (RD_LAMPS_LOWRES | RD_LAMPS_LOWRESMS) : 0u) |
                        (RD_LAMPS_FILLPASTE_DEFAULT ? RD_LAMPS_FILLPASTE : 0u);
    CHECK(rd__lamps_default_bits(1, false, true, true, true) == (ev | pv | RD_LAMPS_AREA),
          "PowerVR");
    CHECK(rd__lamps_default_bits(1, true, true, true, true) == (ev | pv | RD_LAMPS_AREA),
          "PowerVR, a phone");
    CHECK(rd__lamps_default_bits(2, false, true, true, true) == (ev | RD_LAMPS_AREA),
          "a desktop, pass area (the fold of two pairs by the switch alone)");
    CHECK(rd__lamps_default_bits(2, false, true, false, true) == ev, "a desktop, no area");
    CHECK(rd__lamps_default_bits(2, true, true, true, true) == ev, "a faked phone vendor");
    CHECK(rd__lamps_default_bits(0, true, true, true, true) == ev, "Arm, Qualcomm, Apple");
    CHECK(rd__lamps_default_bits(1, false, false, true, true) == ev, "not Vulkan");
    CHECK(rd__lamps_default_bits(1, false, true, false, false) == (ev | pv),
          "no pass area, no fold");
    CHECK(rd__lamps_default_bits(1, false, true, true, false) == (ev | pv | RD_LAMPS_AREA),
          "PowerVR without the blur fold");
    /* t3 */
    const RhiRect e = {0, 0, 0, 0}, a = {10, 20, 30, 40}, c = {100, 5, 10, 10};
    RhiRect u = rd__lamps_union(e, a);
    CHECK(u.x == 10 && u.y == 20 && u.w == 30 && u.h == 40, "empty with a");
    u = rd__lamps_union(a, e);
    CHECK(u.x == 10 && u.y == 20 && u.w == 30 && u.h == 40, "a with empty");
    u = rd__lamps_union(a, c);
    CHECK(u.x == 10 && u.y == 5 && u.w == 100 && u.h == 55, "a with c: %ux%u at %d,%d", u.w, u.h,
          u.x, u.y);
    RhiRect al = rd__lamps_tile_align(u, 32, 2364, 1080);
    CHECK(al.x == 0 && al.y == 0 && al.w == 128 && al.h == 64, "aligned: %ux%u at %d,%d", al.w,
          al.h, al.x, al.y);
    al = rd__lamps_tile_align((RhiRect){2350, 1070, 30, 30}, 32, 2364, 1080);
    CHECK(al.x == 2336 && al.y == 1056 && al.w == 28 && al.h == 24, "clamped: %ux%u at %d,%d", al.w,
          al.h, al.x, al.y);
    /* t4: the corpus's 512 x 512 at the phone's resolution, then the phone's
     * 848 x 448 under Full pixel's crop */
    const RdTargetRec t512 = target(512, 512, 2364, 1080, 2364.0f / 512.0f, 1080.0f / 512.0f, 0.0f);
    boxHolds("512, a small box", &t512, 512, 512, 1800.25, 1805.5, 1810.75, 1820.125);
    boxHolds("512, the left edge", &t512, 512, 512, 1790.0, 1795.0, 1830.5, 1900.0);
    boxHolds("512, wide", &t512, 512, 512, 1792.0, 1792.0, 2303.9, 1900.0);
    const float sx = 2366.0f / (848.0f - 2.0f * 3.73f);
    const RdTargetRec t848 = target(848, 448, 2366, 1080, sx, 1080.0f / 448.0f, 3.73f * sx);
    boxHolds("848 Full pixel, the middle", &t848, 848, 448, 2000.5, 1900.25, 2100.0, 1950.0);
    boxHolds("848 Full pixel, the cut left edge", &t848, 848, 448, 1620.0, 1830.0, 1640.0, 1850.0);
    boxHolds("848 Full pixel, the right edge", &t848, 848, 448, 2440.0, 1830.0, 2475.0, 1850.0);
    RhiRect r;
    CHECK(!rd__lamps_box_texels(&t512, 512, 512, 1000.0, 1000.0, 1100.0, 1100.0, 2, &r),
          "a box left of the target gives none");
    CHECK(rd__lamps_box_texels(&t512, 512, 512, 2290.0, 2290.0, 2400.0, 2400.0, 2, &r) &&
              r.x + (int32_t)r.w <= 2364 && r.y + (int32_t)r.h <= 1080,
          "a box past the corner clipped: %ux%u at %d,%d", r.w, r.h, r.x, r.y);
    /* t5: an orthographic matrix (h = 10 p + 2048 w) and a perspective one
     * (w = 1 + z / 20): VU order, h = m[0] x + m[1] y + m[2] z + m[3] w */
    static const float pts[8][4] = {{-5, -3, 1, 1},   {5, -3, 1, 1},          {-5, 3, 2, 1},
                                    {5, 3, 2, 1},     {0, 0, 4, 1},           {-2.5f, 1, 3, 1},
                                    {4, -2, 1.5f, 1}, {-4.75f, 2.5f, 3.5f, 1}};
    static const float ortho[4][4] = {
        {10, 0, 0, 0}, {0, 10, 0, 0}, {0, 0, 100, 0}, {2048, 2048, 1000, 1}};
    chainHolds("orthographic, 512", ortho, pts, 8, &t512, 512, 512);
    chainHolds("orthographic, 848 Full pixel", ortho, pts, 8, &t848, 848, 448);
    static const float persp[4][4] = {{30, 0, 0, 0},
                                      {0, 30, 0, 0},
                                      {2048.0f / 20.0f, 2048.0f / 20.0f, 100, 0.05f},
                                      {2048, 2048, 1000, 1}};
    chainHolds("perspective, 512", persp, pts, 8, &t512, 512, 512);
    chainHolds("perspective, 848 Full pixel", persp, pts, 8, &t848, 848, 448);
    static const float behind[4][4] = {
        {10, 0, 0, 0}, {0, 10, 0, 0}, {0, 0, 100, -1}, {2048, 2048, 1000, 1}};
    RdVisBox vb;
    RdVisHizBox hb;
    CHECK(rd__vis_box(pts, 8, 1, &vb) && !rd__vis_hiz_occludee(behind, &vb, false, &hb),
          "a corner behind the eye refuses the box");
    /* t7: w = 1 + 0.05 x + 0.05 z: (-10, 0, 30) at w 2, (10, 0, -15) at w
     * 0.75, the bound's corner (-10, 0, -15) at w -0.25 */
    static const float diag[4][4] = {
        {30, 0, 0, 0.05f}, {0, 30, 0, 0}, {0, 0, 100, 0.05f}, {2048, 2048, 1000, 1}};
    static const float two[2][4] = {{-10, 0, 30, 1}, {10, 0, -15, 1}};
    {
        RdVisBox tb;
        RdVisHizBox thb;
        CHECK(rd__vis_box(two, 2, 1, &tb) && !rd__vis_hiz_occludee(diag, &tb, false, &thb),
              "the diagonal's corners' box is refused");
        double gb[4];
        uint32_t behind = 99;
        const int why = rd__lamps_vertex_box(diag, two, 2, 1, false, gb, &behind);
        CHECK(why == RD_LAMPS_BOX_NONE && behind == 0, "the diagonal's vertices: %d, %u behind",
              why, behind);
        RhiRect vr;
        CHECK(rd__lamps_box_texels(&t512, 512, 512, gb[0], gb[1], gb[2], gb[3], 2, &vr),
              "the diagonal's texels");
        int bad = 0;
        for (int k = 0; k <= 256; k++) {
            double h[4], q[4];
            for (int c = 0; c < 4; c++) {
                q[c] = two[0][c] + (two[1][c] - two[0][c]) * k / 256.0;
            }
            for (int c = 0; c < 4; c++) {
                h[c] = diag[0][c] * q[0] + diag[1][c] * q[1] + diag[2][c] * q[2] + diag[3][c];
            }
            int32_t tx, ty;
            texelOf(&t512, 512, 512, h[0] / h[3], h[1] / h[3], &tx, &ty);
            if (tx >= 0 && ty >= 0 && tx < (int32_t)t512.tw && ty < (int32_t)t512.th) {
                bad += !inRect(&vr, tx, ty);
            }
        }
        CHECK(bad == 0, "the diagonal: %d points outside %ux%u at %d,%d", bad, vr.w, vr.h, vr.x,
              vr.y);
        static const float back[2][4] = {{-10, 0, 30, 1}, {-10, 0, -15, 1}};
        CHECK(rd__lamps_vertex_box(diag, back, 2, 1, false, gb, &behind) == RD_LAMPS_BOX_BEHIND &&
                  behind == 1,
              "a vertex behind the eye: %u", behind);
        float bad4[4][4];
        memcpy(bad4, diag, sizeof(bad4));
        bad4[1][2] = NAN;
        CHECK(rd__lamps_vertex_box((const float (*)[4])bad4, two, 2, 1, false, gb, &behind) ==
                  RD_LAMPS_BOX_FINITE,
              "a lane not finite");
    }
    /* t8 */
    {
        RdFold4Axis ax = {30720, 0, 255,  30720, 34816, 8,   4087, 30720, 34799, 8,
                          4104,  8, 4088, 30720, 34800, 256, 256,  256,   0,     0};
        RdFold4Axis ay = {31744, 0, 127,  31744, 33792, 8,   2039, 31744, 33775, 8,
                          2056,  8, 2040, 31744, 33776, 128, 128,  256,   0,     0};
        int32_t w2 = 0, w1 = 0;
        CHECK(rd__lamps_fold4_axis(&ax, &w2, &w1) && w2 == 4 && w1 == 5,
              "the chain's x: windows %d and %d (want 4 and 5)", (int)w2, (int)w1);
        w2 = w1 = 0;
        CHECK(rd__lamps_fold4_axis(&ay, &w2, &w1) && w2 == 4 && w1 == 5,
              "the chain's y: windows %d and %d (want 4 and 5)", (int)w2, (int)w1);
        RdFold4Axis steep = ax;
        steep.b1u1 = 4104 * 3; /* pass 1B reads three WORK0 texels a WORK1 texel */
        w2 = w1 = 0;
        CHECK(!rd__lamps_fold4_axis(&steep, &w2, &w1), "a steep drift refused (windows %d, %d)",
              (int)w2, (int)w1);
        RdFold4Axis zero = ax;
        zero.b1x1 = zero.b1x0;
        CHECK(!rd__lamps_fold4_axis(&zero, &w2, &w1), "a divisor 0 refused");
        CHECK(strcmp(rd__lamps_f4_why_text(RD_LAMPS_F4_LAST), "the chain's last pair next") == 0,
              "the reasons' words");
    }
    /* t6 */
    RhiRect ar = {0, 0, 0, 0};
    CHECK(rd__lamps_area_pick(false, e, a, 2364, 1080, &ar) == RD_LAMPS_WHY_FIRST, "unknown");
    CHECK(rd__lamps_area_pick(true, e, e, 2364, 1080, &ar) == RD_LAMPS_WHY_EMPTY, "nothing");
    CHECK(rd__lamps_area_pick(true, (RhiRect){0, 0, 2340, 1050}, e, 2364, 1080, &ar) ==
              RD_LAMPS_WHY_LARGE,
          "all but a part of a tile");
    const RhiRect lit = {40, 30, 100, 50}, box = {1000, 10, 60, 300};
    CHECK(rd__lamps_area_pick(true, lit, box, 2364, 1080, &ar) == RD_LAMPS_WHY_NONE && ar.x == 32 &&
              ar.y == 0 && ar.w == 1056 && ar.h == 320,
          "the tiles round both: %ux%u at %d,%d", ar.w, ar.h, ar.x, ar.y);
    CHECK(inRect(&ar, lit.x, lit.y) && inRect(&ar, box.x + 59, box.y + 299), "both inside");
    printf("rd_lamps_test: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}

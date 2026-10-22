/* rd_pixel_aa1_test.inc: testAa1Wedge, included by rd_pixel_test.c after
 * testAa1 (rd_pixel_test.c stays well under the 256 KiB file cap). */

/* --------------------------------------------------------- AA1: the wedge
 * A triangle's AA1 edge pixels end where the GS's do (issue 106; port/render/
 * rd_replay_aa1.inc, sprite.hlsl sprite_aa1_ps).  The oracle is PCSX2's
 * software renderer (GSRasterizer.cpp DrawTriangle 683-831: the sort by y,
 * the clockwise test, the top-left flags and the integer edge functions;
 * DrawEdgeTriangle's dispatcher 629-640 and its walk 312-469: the major
 * axis, the side, the stepping decision D, the coverage, the wedge test
 * e12 > 0 && e22 > 0 and the box), written out over the target's 12.4
 * coordinates (pixel k's sample point at 16 k, XYOFFSET subtracted), and the
 * GS's fill (the top-left rule of the same edge functions) for the
 * interiors.  The draw is the drained room's ripple's state (ico2/sugipon/
 * src/puddle.c): PRIM.AA1 and ABE, ALPHA Cs * FIX + Cd with FIX 0x60, Z
 * write, DATE DATM 1, a flat colour of 64: every fragment adds 48, so each
 * pixel's red is 48 times the fragments the GS draws there. */

/* the oracle's fragments per pixel of the 512 x 512 target: count, the
 * last one's As, and (the GS's live DATE, DATM 1) the alpha it leaves and
 * the fragments that test rejects */
static uint8_t s_awN[512][512], s_awLive[512][512], s_awRej[512][512];
static int16_t s_awAs[512][512];

static void awFrag(int x, int y, int as)
{
    if (x < 0 || y < 0 || x >= 512 || y >= 512) {
        return;
    }
    s_awN[y][x]++;
    s_awAs[y][x] = (int16_t)as;
    if (s_awLive[y][x] & 0x80) {
        s_awLive[y][x] = (uint8_t)as;
    } else {
        s_awRej[y][x]++;
    }
}

/* PCSX2's s_ysort */
static const int kAwYSort[8][3] = {{0, 1, 2}, {1, 0, 2}, {0, 0, 0}, {1, 2, 0},
                                   {0, 2, 1}, {0, 0, 0}, {2, 0, 1}, {2, 1, 0}};

/* the GS's fill of triangle a b c (12.4): the pixels whose sample point is
 * inside, or on an edge that is a left edge or a horizontal top edge */
static void awInterior(const int32_t *a, const int32_t *b, const int32_t *c, int as)
{
    const int32_t *p[3] = {a, b, c};
    const int64_t area =
        (int64_t)(b[0] - a[0]) * (c[1] - a[1]) - (int64_t)(b[1] - a[1]) * (c[0] - a[0]);
    if (area == 0) {
        return;
    }
    const int64_t sg = area > 0 ? 1 : -1;
    int64_t ea[3], eb[3], ec[3];
    int tl[3];
    int32_t x0 = a[0], x1 = a[0], y0 = a[1], y1 = a[1];
    for (int i = 0; i < 3; i++) {
        const int32_t *P = p[i], *Q = p[(i + 1) % 3];
        ea[i] = -(int64_t)(Q[1] - P[1]) * sg;
        eb[i] = (int64_t)(Q[0] - P[0]) * sg;
        ec[i] = ((int64_t)P[0] * (Q[1] - P[1]) - (int64_t)P[1] * (Q[0] - P[0])) * sg;
        tl[i] = ea[i] > 0 || (ea[i] == 0 && eb[i] > 0);
        x0 = p[i][0] < x0 ? p[i][0] : x0;
        x1 = p[i][0] > x1 ? p[i][0] : x1;
        y0 = p[i][1] < y0 ? p[i][1] : y0;
        y1 = p[i][1] > y1 ? p[i][1] : y1;
    }
    for (int y = y0 / 16 - 1; y <= y1 / 16 + 1; y++) {
        for (int x = x0 / 16 - 1; x <= x1 / 16 + 1; x++) {
            int in = 1;
            for (int i = 0; i < 3 && in; i++) {
                const int64_t e = ea[i] * (16 * x) + eb[i] * (16 * y) + ec[i];
                in = e > 0 || (e == 0 && tl[i]);
            }
            if (in) {
                awFrag(x, y, as);
            }
        }
    }
}

/* DrawEdgeTriangle: the edge v0 -> v1 (GS pixels) of a triangle whose
 * other two edges' functions are f1, f2 ({x, y, constant} over pixel
 * coordinates); colA the fragment's alpha (0x80: the coverage's) */
static void awEdge(float x0, float y0, float x1, float y1, const int32_t *f1, const int32_t *f2,
                   int tl, int colA)
{
    const float dvx = x1 - x0, dvy = y1 - y0;
    if (dvx == 0.0f && dvy == 0.0f) {
        return;
    }
    const int stepX = fabsf(dvx) >= fabsf(dvy);
    const int posX = dvx >= 0.0f, posY = dvy >= 0.0f;
    const int side = tl ^ (stepX && dvy != 0.0f && posX == posY);
    const float rx0 = posX ? ceilf(x0 - 1.0f) : floorf(x0 + 1.0f);
    const float ry0 = posY ? ceilf(y0 - 1.0f) : floorf(y0 + 1.0f);
    const float rx1 = posX ? floorf(x1 + 1.0f) : ceilf(x1 - 1.0f);
    const float ry1 = posY ? floorf(y1 + 1.0f) : ceilf(y1 - 1.0f);
    const int rxi1 = (int)rx1, ryi1 = (int)ry1;
    int bxi0 = (int)floorf(fminf(x0, x1)), byi0 = (int)ceilf(fminf(y0, y1) - 1.0f);
    int bxi1 = (int)ceilf(fmaxf(x0, x1)), byi1 = (int)floorf(fmaxf(y0, y1) + 1.0f);
    /* the scissor: the target */
    bxi0 = bxi0 < 0 ? 0 : bxi0;
    byi0 = byi0 < 0 ? 0 : byi0;
    bxi1 = bxi1 > 511 ? 511 : bxi1;
    byi1 = byi1 > 511 ? 511 : byi1;
    const int dxi = posX ? 1 : -1, dyi = posY ? 1 : -1;
    const int posD = stepX ? posY : posX;
    const int scaleD = (int)(2 * 16 * 16 * fabsf(stepX ? dvx : dvy));
    const float scaleDf = (float)scaleD;
    const int dD = (int)(2 * 16 * 16 * (stepX ? dvy : dvx));
    int D = (int)(scaleD * (stepX ? (y0 - ry0) : (x0 - rx0)));
    int xi = (int)rx0, yi = (int)ry0;
    int e1 = f1[0] * xi + f1[1] * yi + f1[2];
    int e2 = f2[0] * xi + f2[1] * yi + f2[2];
#define AW_STEP_DEPENDENT(sign)                                                                    \
    do {                                                                                           \
        D -= scaleD * (sign);                                                                      \
        xi += (stepX ? 0 : 1) * (sign);                                                            \
        yi += (stepX ? 1 : 0) * (sign);                                                            \
        e1 += (stepX ? f1[1] : f1[0]) * (sign);                                                    \
        e2 += (stepX ? f2[1] : f2[0]) * (sign);                                                    \
    } while (0)
    const float prestep = stepX ? (float)dxi * (rx0 - x0) : (float)dyi * (ry0 - y0);
    D += (int)((float)dD * prestep);
    while (D >= scaleD / 2) {
        AW_STEP_DEPENDENT(1);
    }
    while (D < -scaleD / 2) {
        AW_STEP_DEPENDENT(-1);
    }
    for (int guard = 0; guard < 4096; guard++) {
        const float d = (float)D / scaleDf;
        int cov, off;
        if (d > 0.0f) {
            cov = (int)(0xffff * (side ? 1.0 - d : d));
            off = side ? 0 : 1;
        } else if (d < 0.0f) {
            cov = (int)(0xffff * (side ? -d : 1.0 + d));
            off = side ? -1 : 0;
        } else {
            cov = tl ? 0 : 0xffff;
            off = tl ? (side ? -1 : 1) : 0;
        }
        const int xi2 = xi + (stepX ? 0 : off), yi2 = yi + (stepX ? off : 0);
        const int e12 = e1 + (stepX ? f1[1] : f1[0]) * off;
        const int e22 = e2 + (stepX ? f2[1] : f2[0]) * off;
        if (e12 > 0 && e22 > 0 && bxi0 <= xi2 && xi2 <= bxi1 && byi0 <= yi2 && yi2 <= byi1) {
            cov = cov < 0 ? 0 : (cov > 0xffff ? 0xffff : cov);
            awFrag(xi2, yi2, colA == 0x80 ? cov >> 9 : colA);
        }
        if (stepX ? (xi == rxi1) : (yi == ryi1)) {
            break;
        }
        D += dD;
        xi += stepX ? dxi : 0;
        yi += stepX ? 0 : dyi;
        e1 += stepX ? dxi * f1[0] : dyi * f1[1];
        e2 += stepX ? dxi * f2[0] : dyi * f2[1];
        if (posD) {
            if (D >= scaleD / 2) {
                AW_STEP_DEPENDENT(1);
            }
        } else if (D < -scaleD / 2) {
            AW_STEP_DEPENDENT(-1);
        }
    }
#undef AW_STEP_DEPENDENT
}

/* f = the edge function of a -> b, {x, y, constant} over pixel
 * coordinates (PCSX2's f0, f1, f2) */
static void awEdgeFn(const int32_t *a, const int32_t *b, int32_t *f)
{
    f[0] = 16 * (b[1] - a[1]);
    f[1] = -16 * (b[0] - a[0]);
    f[2] = b[0] * a[1] - a[0] * b[1];
}

/* DrawTriangle with PRIM.AA1: the interior, then the edges v0-v1, v0-v2,
 * v1-v2 of the vertices sorted by y */
static void awTriangle(const int32_t *a, const int32_t *b, const int32_t *c, int colA)
{
    const int32_t *t[3] = {a, b, c};
    const int m = (a[1] > b[1]) | (a[1] > c[1]) << 1 | (b[1] > c[1]) << 2;
    const int32_t *v0 = t[kAwYSort[m][0]], *v1 = t[kAwYSort[m][1]], *v2 = t[kAwYSort[m][2]];
    if (v0[1] == v1[1] && v1[1] == v2[1]) {
        return;
    }
    const int64_t cross =
        (int64_t)(v1[0] - v0[0]) * (v2[1] - v0[1]) - (int64_t)(v1[1] - v0[1]) * (v2[0] - v0[0]);
    if (cross == 0) {
        return;
    }
    awInterior(a, b, c, colA);
    const int cw = cross > 0;
    const int tl0 = v0[1] == v1[1] || !cw, tl1 = cw, tl2 = v1[1] != v2[1] && !cw;
    int32_t f0[3], f1[3], f2[3];
    awEdgeFn(v0, v1, f0);
    awEdgeFn(v2, v0, f1);
    awEdgeFn(v1, v2, f2);
    for (int i = 0; cw && i < 3; i++) {
        f0[i] = -f0[i];
        f1[i] = -f1[i];
        f2[i] = -f2[i];
    }
    f0[2] += tl0;
    f1[2] += tl1;
    f2[2] += tl2;
    const float p0x = (float)v0[0] / 16.0f, p0y = (float)v0[1] / 16.0f;
    const float p1x = (float)v1[0] / 16.0f, p1y = (float)v1[1] / 16.0f;
    const float p2x = (float)v2[0] / 16.0f, p2y = (float)v2[1] / 16.0f;
    awEdge(p0x, p0y, p1x, p1y, f1, f2, tl0, colA);
    awEdge(p0x, p0y, p2x, p2y, f2, f0, tl1, colA);
    awEdge(p1x, p1y, p2x, p2y, f0, f1, tl2, colA);
}

/* the strips (12.4, relative to the target's top-left): four vertices
 * with an acute corner at v1, and an octagonal ring of eight quads like
 * the ripples' (centre (128.3, 180.7), radii 40 and 34, squashed 0.6 in y) */
static const int32_t kAwAcute[4][2] = {{325, 489}, {3693, 611}, {487, 815}, {2409, 1765}};
static const int32_t kAwRing[18][2] = {
    {2664, 3004}, {2572, 2987}, {2351, 3230}, {2306, 3179}, {1863, 3258}, {1892, 3203},
    {1486, 3070}, {1571, 3043}, {1441, 2777}, {1533, 2794}, {1754, 2551}, {1798, 2602},
    {2241, 2524}, {2213, 2579}, {2618, 2712}, {2533, 2738}, {2664, 3004}, {2572, 2987}};

/* one strip drawn (dx: added to x, 12.4) and fed to the oracle */
static void awStrip(const int32_t (*s)[2], int n, int32_t dx, const uint8_t col[4])
{
    const int32_t ox = (2048 - 256) * 16, oy = (2048 - 256) * 16;
    RdScreenVtx v[18];
    int32_t p[18][2];
    for (int i = 0; i < n && i < 18; i++) {
        p[i][0] = s[i][0] + dx;
        p[i][1] = s[i][1];
        v[i] = vtx(ox + p[i][0], oy + p[i][1], 0, col, 0, 0);
    }
    rd_screen_prims(RD_PRIM_TRIANGLE_STRIP, v, (uint32_t)n, RD_SPACE_WORLD, 1, 0);
    for (int i = 0; i + 2 < n; i++) {
        awTriangle(p[i], p[i + 1], p[i + 2], col[3]);
    }
}

static void testAa1Wedge(void)
{
    static const uint8_t clr[4] = {0, 0, 0, 0x80};
    static const uint8_t opaque[4] = {64, 64, 64, 0xFF}, half[4] = {64, 64, 64, 0x80};
    memset(s_awN, 0, sizeof(s_awN));
    memset(s_awRej, 0, sizeof(s_awRej));
    memset(s_awLive, 0x80, sizeof(s_awLive));
    memset(s_awAs, 0, sizeof(s_awAs));
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_texture_off();
    rd_aa1(1);
    rd_abe(1);
    rd_blend(RD_BLEND_CS_FIX_ADD_CD, 0x60, 1);
    rd_z_write(1);
    rd_test_gs(0x3C000); /* DATE DATM 1, Z ALWAYS */
    /* columns 0..255: alpha 0xFF (no coverage alpha, nothing for DATE to
     * reject: the GS and the port's per-draw DATE agree); 256..511 the same
     * strips at alpha 0x80 (the coverage alpha, VD-17) */
    awStrip(kAwAcute, 4, 0, opaque);
    awStrip(kAwRing, 18, 0, opaque);
    awStrip(kAwAcute, 4, 256 * 16, half);
    awStrip(kAwRing, 18, 256 * 16, half);
    rd_aa1(0);
    opaque2D();
    rd_end_frame(0);
    uint32_t w, h;
    const uint8_t *img = readTarget(RD_TARGET_SCENE, &w, &h);
    if (!img) {
        return;
    }
    CHECK(w >= 512 && h >= 256, "aa1 wedge: SCENE %ux%u", w, h);
    if (w < 512 || h < 256) {
        return;
    }
    int bad[2] = {0, 0}, n1[2] = {0, 0}, n2[2] = {0, 0}, n3[2] = {0, 0};
    int maxd = 0, edge1 = 0, rejected = 0, rejectedPx = 0;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 512; x++) {
            const int half2 = x >= 256;
            const int n = s_awN[y][x];
            const uint8_t *p = img + ((size_t)y * w + (size_t)x) * 4;
            const int want = 48 * n > 255 ? 255 : 48 * n;
            n1[half2] += n == 1;
            n2[half2] += n == 2;
            n3[half2] += n >= 3;
            if (p[0] != want || p[1] != want || p[2] != want) {
                if (bad[half2]++ < 8) {
                    printf("FAIL aa1 wedge (%d,%d): %u, expected %d (%d fragments in the GS)\n", x,
                           y, p[0], want, n);
                }
            }
            if (!half2 && n > 0 && p[3] != 0xFF && bad[0]++ < 8) {
                printf("FAIL aa1 wedge (%d,%d): alpha %u, expected 0xFF\n", x, y, p[3]);
            }
            if (half2 && n == 1) {
                /* one fragment: its As is the alpha written */
                const int d = abs((int)p[3] - s_awAs[y][x]);
                maxd = d > maxd ? d : maxd;
                edge1 += s_awAs[y][x] < 0x80;
            }
            if (half2 && s_awRej[y][x]) {
                rejected += s_awRej[y][x];
                rejectedPx++;
            }
        }
    }
    printf("  aa1 wedge, alpha 0xFF: GS fragments 1 at %d pixels, 2 at %d, 3 or more at %d; %d "
           "pixels off\n",
           n1[0], n2[0], n3[0], bad[0]);
    printf("  aa1 wedge, alpha 0x80: 1 at %d, 2 at %d, 3+ at %d; %d pixels off; As of the %d "
           "single edge pixels within %d LSB\n",
           n1[1], n2[1], n3[1], bad[1], edge1, maxd);
    printf("  aa1 wedge, alpha 0x80 (VD-17): the GS's own DATE rejects %d later fragments at %d "
           "pixels (an earlier edge pixel left alpha below 0x80); the port's per-draw DATE "
           "draws them\n",
           rejected, rejectedPx);
    CHECK(bad[0] == 0 && n1[0] > 9000 && n2[0] > 700 && n3[0] > 0,
          "aa1 wedge, alpha 0xFF: %d pixels off the GS's (%d/%d/%d)", bad[0], n1[0], n2[0], n3[0]);
    CHECK(bad[1] == 0 && n1[1] == n1[0] && n2[1] == n2[0],
          "aa1 wedge, alpha 0x80: %d pixels off the GS's count", bad[1]);
    CHECK(maxd <= 1 && edge1 > 500, "aa1 wedge: As of single edge pixels: %d LSB off (%d pixels)",
          maxd, edge1);
}

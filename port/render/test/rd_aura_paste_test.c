/* rd_aura_paste_test.c: the aura's paste split's row proof, no device
 * (port/render/rd_aura_paste.c; docs/port/RENDER_POST.md, "The aura's paste
 * as its constant part").
 *
 *   rd_aura_paste_test
 *
 *   t1  rd__ap_taps: the GS's nearest and bilinear taps (a fraction of 0
 *       leaves the second tap out), the B-spline's four, the box's span
 *   t2  rd__ap_v: the GS's V, truncated, above and below the rect's top
 *   t3  rd__ap_proven: CLAMP to the TEX0 rows, REPEAT (a moved row proven only
 *       when every row is), the backing texture's clamp, a span over 64 rows
 *       refused
 *   t4  the Pixel's frame v3677 (848 x 448 GS, SCENE 1080 rows at 2.411, the
 *       aura's sprites as its list has them): WORK0 rows 0-111 (8:39's
 *       coverage) carried into FEED128 by 8:47's bilinear at scale 1 (V = 16 y:
 *       rows y - 1 and y at a fraction of 8): rows 0-111 under CLAMP, 1-111
 *       under REPEAT; the paste's (8:55, B-spline at 3/32 texel a texel)
 *       rows drawn as the alpha alone: [0, 1062) with both on CLAMP, [11,
 *       1062) with the paste on REPEAT, [21, 1062) with 8:47 on REPEAT (a
 *       Python mirror of rd_aura_paste.c's arithmetic gave these; the
 *       measured band of the cage frame, rows 1072-1079, lies in the band). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_aura_paste.h"

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

static void taps(int32_t c16, int32_t step32, bool bil, int32_t wantLo, int32_t wantHi)
{
    int32_t lo = 0, hi = 0;
    rd__ap_taps(c16, step32, bil, &lo, &hi);
    CHECK(lo == wantLo && hi == wantHi, "taps(%d, %d, %d): %d..%d, %d..%d expected", c16, step32,
          (int)bil, lo, hi, wantLo, wantHi);
}

static void rows(RdApRows *r, uint32_t n, int32_t a, int32_t b)
{
    memset(r, 0, sizeof(*r));
    r->n = n;
    for (int32_t y = a; y < b; y++) {
        rd__ap_rows_set(r, y);
    }
}

/* 8:47 at scale 1: FEED128's rows whose bilinear taps lie in w0's rows */
static void carry(const RdApRows *w0, bool clampT, RdApRows *feed)
{
    static const float rect[4] = {31072.0f, 31744.0f, 34464.0f, 33792.0f};
    static const float uv[4] = {0.0f, 0.0f, 6784.0f, 2048.0f};
    const int32_t oy16 = 16 * (2048 - 64);
    memset(feed, 0, sizeof(*feed));
    feed->n = 128;
    for (int32_t y = 0; y < 128; y++) {
        const int32_t gy = oy16 + 16 * y;
        if (gy < (int32_t)rect[1] || gy >= (int32_t)rect[3]) {
            continue;
        }
        int32_t lo, hi;
        rd__ap_taps(rd__ap_v(gy, rect, uv), 0, true, &lo, &hi);
        if (rd__ap_proven(w0, lo, hi, clampT, 128, 128)) {
            rd__ap_rows_set(feed, y);
        }
    }
}

static void paste(const RdApRows *feed, bool clampT, uint32_t want0, uint32_t want1)
{
    RdApPaste in;
    memset(&in, 0, sizeof(in));
    in.th = 1080;
    in.sy = 1080.0f / 448.0f;
    in.ssy = 1.0f;
    in.oy16 = 16 * (2048 - 224);
    const float rect[4] = {25984.0f, 29184.0f, 39552.0f, 36352.0f};
    const float uv[4] = {8.0f, 8.0f, 3400.0f, 1800.0f};
    memcpy(in.rect, rect, sizeof(rect));
    memcpy(in.uv, uv, sizeof(uv));
    in.step32 = 3;
    in.bilinear = true;
    in.clampT = clampT;
    in.lsz = 128;
    in.asz = 128;
    in.proven = feed;
    uint32_t c0 = 0, c1 = 0;
    const bool ok = rd__ap_paste_rows(&in, &c0, &c1);
    printf("  the paste (%s): rows [%u, %u) as the alpha alone, %u band rows\n",
           clampT ? "CLAMP" : "REPEAT", c0, c1, 1080u - (c1 - c0));
    CHECK(ok && c0 == want0 && c1 == want1, "rows [%u, %u), [%u, %u) expected", c0, c1, want0,
          want1);
    CHECK(c1 <= 1072u, "the measured band (rows 1072-1079) not in the band: c1 %u", c1);
}

int main(void)
{
    printf("rd_aura_paste_test\n");
    /* t1 */
    taps(16 * 5, 0, false, 5, 5);     /* nearest */
    taps(16 * 5 + 8, 0, true, 5, 5);  /* bilinear at a fraction of 0 */
    taps(16 * 5, 0, true, 4, 5);      /* bilinear at 8 */
    taps(16 * 5 + 8, 3, true, 4, 7);  /* the B-spline */
    taps(16 * 5 + 8, 3, false, 5, 5); /* nearest at a magnifying step */
    taps(16 * 10, 128, true, 8, 11);  /* the box of 4 texels about 10.0 */
    /* t2 */
    static const float rect[4] = {0.0f, 160.0f, 1600.0f, 320.0f};
    static const float uv[4] = {0.0f, 8.0f, 1600.0f, 168.0f};
    CHECK(rd__ap_v(160, rect, uv) == 8 && rd__ap_v(320, rect, uv) == 168 &&
              rd__ap_v(161, rect, uv) == 9 && rd__ap_v(159, rect, uv) == 7,
          "rd__ap_v at the edges");
    static const float uvh[4] = {0.0f, 0.0f, 1600.0f, 80.0f};
    CHECK(rd__ap_v(163, rect, uvh) == 1 && rd__ap_v(157, rect, uvh) == -1,
          "rd__ap_v truncates toward 0: %d and %d", rd__ap_v(163, rect, uvh),
          rd__ap_v(157, rect, uvh));
    /* t3 */
    RdApRows p;
    rows(&p, 128, 0, 112);
    CHECK(rd__ap_proven(&p, -2, 1, true, 128, 128), "CLAMP: rows -2..1 read row 0");
    CHECK(!rd__ap_proven(&p, -2, 1, false, 128, 128), "REPEAT: rows -2..1 read rows 126, 127");
    CHECK(!rd__ap_proven(&p, 110, 113, true, 128, 128), "row 112 not proven");
    CHECK(rd__ap_proven(&p, 100, 111, true, 128, 128), "rows 100-111 proven");
    RdApRows all;
    rows(&all, 128, 0, 128);
    /* REPEAT outside the texture's rows, every row proven: proven, within
     * the 64-row span rd__ap_proven reads at most (a wider kernel is not
     * proven: the paste drawn whole) */
    CHECK(rd__ap_proven(&all, -3, 60, false, 128, 128) &&
              rd__ap_proven(&all, 100, 130, false, 128, 128),
          "every row proven: REPEAT above and below the texture");
    CHECK(!rd__ap_proven(&all, -3, 130, false, 128, 128), "a span of 134 rows (over 64) proven");
    CHECK(rd__ap_proven(&p, 115, 120, true, 112, 128),
          "CLAMP at 112 TEX0 rows: rows 115-120 read row 111, proven");
    /* t4 */
    RdApRows w0, feed;
    rows(&w0, 128, 0, 112);
    carry(&w0, true, &feed);
    int32_t a = -1, b = -1;
    for (int32_t y = 0; y < 128; y++) {
        if (rd__ap_rows_has(&feed, y)) {
            a = a < 0 ? y : a;
            b = y;
        }
    }
    printf("  FEED128 (CLAMP): rows %d-%d proven alpha 0\n", a, b);
    CHECK(a == 0 && b == 111, "FEED128's rows %d-%d, 0-111 expected", a, b);
    paste(&feed, true, 0u, 1062u);
    paste(&feed, false, 11u, 1062u);
    carry(&w0, false, &feed);
    a = -1;
    for (int32_t y = 0; y < 128 && a < 0; y++) {
        a = rd__ap_rows_has(&feed, y) ? y : -1;
    }
    CHECK(a == 1, "FEED128 (REPEAT): first row %d, 1 expected", a);
    paste(&feed, true, 21u, 1062u);
    if (failures) {
        printf("rd_aura_paste_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_aura_paste_test: ok\n");
    return 0;
}

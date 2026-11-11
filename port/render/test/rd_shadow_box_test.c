/* rd_shadow_box_test.c: the shadows' boxes (rd_shadow_box.c), CPU only.
 *
 *   1. the count's box from 12.4 vertices: the extent's pixels,
 *      RD_SB_COUNT_MARGIN wider each side, clipped to the target;
 *   2. one axis through a sprite: the Pixel frame's level 0 (2:1 from the
 *      848x448 count onto 424 columns, UVs 0.25..848.25), its rows reading
 *      past the count's 448 (0.25..512.25: a clamped box at the edge takes
 *      every row past it), a repeated axis at an edge taken whole, a box no
 *      UV reaches (none), a constant UV;
 *   3. whole-cover of a level (the GS's ceil rule), one pixel short not;
 *   4. a composite: SHADOW2 (106x64) over the 848x448 frame;
 *   5. union and clip;
 *   6. the 10 s clause and the --stats words from a made-up record. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_shadow_box.h"

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

static RdScreenVtx vtx(float x, float y, float u, float v)
{
    RdScreenVtx p;
    memset(&p, 0, sizeof(p));
    p.x = (int32_t)lroundf(x * 16.0f);
    p.y = (int32_t)lroundf(y * 16.0f);
    p.s = u * 16.0f;
    p.t = v * 16.0f;
    p.q = 1.0f;
    return p;
}

static void testCount(void)
{
    /* origin 2048 - 424 = 1624, 2048 - 224 = 1824 (848x448) */
    const int32_t ox16 = 1624 * 16, oy16 = 1824 * 16;
    RdSbExtent e;
    memset(&e, 0, sizeof(e));
    RdSbBox b = rd_sb_count_box(&e, 848, 448);
    CHECK(b.empty, "no vertex: empty");
    /* GS pixels 100.5 .. 200.25 across, 50 .. 60.0625 down */
    rd_sb_extent_add(&e, (1624 + 100) * 16 + 8, (1824 + 50) * 16, ox16, oy16);
    rd_sb_extent_add(&e, (1624 + 200) * 16 + 4, (1824 + 60) * 16 + 1, ox16, oy16);
    b = rd_sb_count_box(&e, 848, 448);
    CHECK(!b.empty && b.x0 == 100 - RD_SB_COUNT_MARGIN && b.x1 == 201 + RD_SB_COUNT_MARGIN &&
              b.y0 == 50 - RD_SB_COUNT_MARGIN && b.y1 == 61 + RD_SB_COUNT_MARGIN,
          "count box %d..%d x %d..%d", b.x0, b.x1, b.y0, b.y1);
    /* a vertex off the left edge (negative 12.4 clamps to 0, GS -1624) and
       one past the right: clipped to the target */
    rd_sb_extent_add(&e, -500, (1824 + 10) * 16, ox16, oy16);
    rd_sb_extent_add(&e, (1624 + 900) * 16, (1824 + 10) * 16, ox16, oy16);
    b = rd_sb_count_box(&e, 848, 448);
    CHECK(!b.empty && b.x0 == 0 && b.x1 == 847 && b.y0 == 10 - RD_SB_COUNT_MARGIN,
          "clipped count box %d..%d x %d..%d", b.x0, b.x1, b.y0, b.y1);
}

static void testAxis(void)
{
    int32_t d0 = 0, d1 = 0;
    /* level 0 across: x -0.25 .. 423.75, u 0.25 .. 848.25, box 100..200:
       u in [97, 204]: x = -0.25 + (u - 0.25) / 2 in [48.125, 101.625] */
    bool ok = rd_sb_map_axis(-0.25f, 423.75f, 0.25f, 848.25f, 100, 200, 848, false, &d0, &d1);
    CHECK(ok && d0 == 48 - RD_SB_PIX_MARGIN && d1 == 102 + RD_SB_PIX_MARGIN, "level x %d..%d", d0,
          d1);
    /* down: y -0.25 .. 255.75, v 0.25 .. 512.25 over 448 rows, box 400..447
       (the last row): clamped, every v past 397 reads it: y from 198.125
       to the sprite's end 255.75 (s1 = 257) */
    ok = rd_sb_map_axis(-0.25f, 255.75f, 0.25f, 512.25f, 400, 447, 448, false, &d0, &d1);
    CHECK(ok && d0 == 198 - RD_SB_PIX_MARGIN && d1 == 257, "level y at the edge %d..%d", d0, d1);
    /* repeated and at the edge: the whole axis (the sprite's pixels -2..425) */
    ok = rd_sb_map_axis(-0.25f, 423.75f, 0.25f, 848.25f, 0, 10, 848, true, &d0, &d1);
    CHECK(ok && d0 == -2 && d1 == 425, "repeat at the edge: %d..%d", d0, d1);
    /* repeated, inside and UVs inside: as clamped */
    ok = rd_sb_map_axis(-0.25f, 423.75f, 0.25f, 847.75f, 100, 200, 848, true, &d0, &d1);
    CHECK(ok && d0 >= 44 && d0 <= 46 && d1 >= 104 && d1 <= 106, "repeat inside: %d..%d", d0, d1);
    /* UVs 0..10 never come within 3 texels of 100..110 */
    ok = rd_sb_map_axis(0.0f, 100.0f, 0.0f, 10.0f, 100, 110, 848, false, &d0, &d1);
    CHECK(!ok, "a box no UV reaches: none");
    /* a constant UV inside the box's reach: every pixel of the sprite */
    ok = rd_sb_map_axis(10.0f, 20.0f, 105.0f, 105.0f, 100, 110, 848, false, &d0, &d1);
    CHECK(ok && d0 == 9 && d1 == 21, "constant UV %d..%d", d0, d1);
    /* a flipped sprite (u falling as x rises) maps the same pixels */
    ok = rd_sb_map_axis(423.75f, -0.25f, 0.25f, 848.25f, 100, 200, 848, false, &d0, &d1);
    CHECK(ok && d0 >= 320 - RD_SB_PIX_MARGIN && d1 <= 376 + RD_SB_PIX_MARGIN, "flipped %d..%d", d0,
          d1);
}

static void testCovers(void)
{
    /* SHADOW0 424x256, origin 2048 - 212 = 1836, 2048 - 128 = 1920 */
    const int32_t ox16 = 1836 * 16, oy16 = 1920 * 16;
    RdScreenVtx a = vtx(1835.75f, 1919.75f, 0.25f, 0.25f),
                b = vtx(2259.75f, 2175.75f, 848.25f, 512.25f);
    CHECK(rd_sb_covers(&a, &b, ox16, oy16, 424, 256), "the Pixel's level 0 covers SHADOW0");
    RdScreenVtx s = vtx(2258.75f, 2175.75f, 848.25f, 512.25f);
    CHECK(!rd_sb_covers(&a, &s, ox16, oy16, 424, 256), "one pixel short does not");
}

static void testComposite(void)
{
    /* SHADOW2 106x64 sampled 0.25..106.25 x 0.25..56.25 over the frame
       1624..2472 x 1824..2272 (848x448 from 1624, 1824); box 10..20 x 5..8:
       u in [7, 24]: x = (u - 0.25) * 8 in [54, 190]; v in [2, 12]:
       y = (v - 0.25) * 8 in [14, 94] */
    const RdScreenVtx a = vtx(1624.0f, 1824.0f, 0.25f, 0.25f),
                      b = vtx(2472.0f, 2272.0f, 106.25f, 56.25f);
    const RdSbBox src = {10, 5, 20, 8, 0};
    RdSbBox d =
        rd_sb_map_sprite(&a, &b, 0.0f, 0.0f, 1624 * 16, 1824 * 16, &src, 106, 64, false, false);
    CHECK(!d.empty && d.x0 == 54 - RD_SB_PIX_MARGIN && d.x1 == 190 + RD_SB_PIX_MARGIN &&
              d.y0 == 14 - RD_SB_PIX_MARGIN && d.y1 == 94 + RD_SB_PIX_MARGIN,
          "composite box %d..%d x %d..%d", d.x0, d.x1, d.y0, d.y1);
    const RdSbBox none = {0, 0, 0, 0, 1};
    d = rd_sb_map_sprite(&a, &b, 0.0f, 0.0f, 1624 * 16, 1824 * 16, &none, 106, 64, false, false);
    CHECK(d.empty, "an empty source: nothing");
    /* the UV offset moves the map: u + 1 texel moves x by 8 pixels left */
    d = rd_sb_map_sprite(&a, &b, 1.0f, 0.0f, 1624 * 16, 1824 * 16, &src, 106, 64, false, false);
    CHECK(!d.empty && d.x0 == 46 - RD_SB_PIX_MARGIN && d.x1 == 182 + RD_SB_PIX_MARGIN,
          "with the UV offset %d..%d", d.x0, d.x1);
}

static void testUnionClip(void)
{
    const RdSbBox a = {10, 10, 20, 20, 0}, b = {-5, 15, 30, 900, 0}, e = {0, 0, 0, 0, 1};
    RdSbBox u = rd_sb_union(a, b);
    CHECK(u.x0 == -5 && u.y0 == 10 && u.x1 == 30 && u.y1 == 900 && !u.empty, "union");
    u = rd_sb_union(e, a);
    CHECK(!u.empty && u.x0 == 10 && u.x1 == 20, "union with empty");
    RdSbBox c = rd_sb_clip(b, 848, 448);
    CHECK(!c.empty && c.x0 == 0 && c.y1 == 447 && c.x1 == 30, "clip");
    c = rd_sb_clip((RdSbBox){900, 0, 950, 10, 0}, 848, 448);
    CHECK(c.empty, "clip outside: empty");
}

static void testLines(void)
{
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.shadowBoxCounts = 1;
    rec.shadowBoxComposites = 3;
    rec.shadowBoxTexels = 25;
    rec.shadowBoxAllTexels = 100;
    rec.shadowBoxWhy[RD_SB_WHY_SEVEN] = 1;
    rd__shadow_box_pop(&rec);
    char text[512];
    size_t at = 0;
    text[0] = '\0';
    rd__shadow_box_text(text, sizeof text, &at);
    printf("  10 s clause: %s\n", text);
    CHECK(strcmp(text, "; shadow box: 1.0 counts and 3.0 composites a replay over the volumes' "
                       "box (25 % of their picture), 1.0 whole (seven draws 1.0)") == 0,
          "the 10 s clause: %s", text);
    const size_t was = at;
    rd__shadow_box_text(text, sizeof text, &at);
    CHECK(at == was, "a second clause without a record");
    char st[256];
    rd__shadow_box_stats(&rec, st, sizeof st);
    printf("  --stats: %s\n", st);
    CHECK(strcmp(st, "shadow box on: 1 counts and 3 composites over the volumes' box (0 with "
                     "nothing to draw; 25 of 100 texels), 1 whole (seven draws 1)") == 0,
          "the stats words: %s", st);
}

int main(void)
{
    printf("rd_shadow_box_test\n");
    testCount();
    testAxis();
    testCovers();
    testComposite();
    testUnionClip();
    testLines();
    if (failures) {
        printf("rd_shadow_box_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_box_test: ok\n");
    return 0;
}

/* rd_vu_zero_test.c: the zero-area cull's pure parts (port/render/rd_vu_zero.c,
 * rd_vu_zero.h; issue 179), on the CPU:
 *   t1  the lattice word of the targets the run draws on: 1x at 16:9
 *       (688 x 512), the field's half line (RD_TARGET_HALF_Y), 2x and 3x,
 *       an odd width, the Full pixel crop at an even and an odd width; the
 *       refusals (not the exact viewport, the MSAA epoch's edges, jitter,
 *       off the grid, a scale not whole, a crop's scale not whole), each
 *       with its reason; against the texel centres the exact viewport's
 *       mapping puts on the GS grid, worked out by hand in the comments
 *   t2  the test against a brute-force rasteriser: random small
 *       triangles on three lattices, every corner moved by every
 *       combination of -1, 0 and 1 sixteenth (more than the snap of a GPU
 *       with 4 sub-pixel bits), every point on an edge covered (any fill
 *       rule): a triangle the test culls covers no sample point under any
 *       of them; the test culls some, keeps some that cover nothing under
 *       the top-left rule (the grown margin), never culls one that covers a
 *       point unmoved
 *   t3  the cases of the device's self-test (rd_vu_zero_selftest.c) at 1x:
 *       inside both windings culled, a corner on a point kept, an edge
 *       through one kept, no area between points culled and through one
 *       kept, a big one kept, one within the margin kept, a sliver whose
 *       box is too wide kept; the target's left edge
 *   t4  the record check (rd_vu_zero_check): a record written as the rule
 *       stage writes it agrees in every slot; a flag flipped, a flag
 *       without a word and a slot whose corner has no GS position are
 *       found, the first in words
 *   t5  vu_ftoi4's edges (rd_vu_zero_ftoi4)
 *   t6  the run's state and texts (rd_vu_zero_run.c): held until the
 *       self-test, on when it passes, ICO_RD_VU_ZERO=0 off, =1 on, the
 *       loss ladder (ICO_LOSS_ROW=1) off for this start over =1, the block
 *       clause and its reset (the checks' clause only where a check ran),
 *       the words a velocity, skinned or probe entry gives up, no zero
 *       module made without a try, a failed self-test off for the run and
 *       still off at a renderer made again (over =1), the install's mark
 *       (ICO_VU_ZERO_BLOCK) off over =1, the reasons' words */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_zero.h"
#include "shader_consts.h"

static int s_fail;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void putEnv(const char *name, const char *value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value ? value : "");
#else
    if (value) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

static uint32_t mkWord(uint32_t kx, uint32_t ky, uint32_t sx, uint32_t sy)
{
    return kx | (ky << 4) | (sx << 8) | (sy << 12) | ICO_VU_ZERO_ON;
}

/* a frame-wide target at scale k, as rd_core.c rd__target_scale_of and
   rd__replay_vu_frame make it: GS w x h, origin 2048 - w / 2 */
static RdVuZeroMap frameMap(uint32_t w, uint32_t h, uint32_t k)
{
    RdVuZeroMap m;
    memset(&m, 0, sizeof(m));
    m.w = w;
    m.h = h;
    m.tw = w * k;
    m.th = h * k;
    m.sx = m.sy = (float)k;
    m.originX = 2048.0f - (float)(w >> 1);
    m.originY = 2048.0f - (float)(h >> 1);
    m.extentMax = 16384u;
    m.exact = true;
    return m;
}

static void t1(void)
{
    int why = 0;
    /* 1x, 688 x 512: P 16384, vpX = floor((688 - 16384) / 2) = -7848, vpo 0,
       c 2048: K = 16 (-7848 + 8192 + 0.5) - 16 * 2048 - 8 = -27264 = 0 mod
       16: the texel centres at GS integer coordinates (U = g) */
    RdVuZeroMap m = frameMap(688, 512, 1);
    uint32_t w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 1, 1), "1x 688x512: word 0x%05X, why %d", w, why);
    CHECK(why == -1, "1x: why %d", why);
    /* the field's half line: origin y + 0.5, c 2048.5: K = 16 * 256.5 -
       16 * 2048.5 - 8 = -28680 = 8 mod 16: the centres half a GS pixel down */
    m.originY += 0.5f;
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 8, 1, 1), "1x half line: word 0x%05X", w);
    /* 2x: tw 1376, vpX -7504, vpo 0: K = 16 * 688.5 - 32 * 2048 - 8 = -54528 =
       0 mod 16: U = 2 g, the centres at every half GS pixel */
    m = frameMap(688, 512, 2);
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 2, 2), "2x: word 0x%05X", w);
    /* 3x: tw 2064, vpX -7160, vpo 0: K = 16 * 1032.5 - 48 * 2048 - 8 = -81792 */
    m = frameMap(688, 512, 3);
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 3, 3), "3x: word 0x%05X", w);
    /* an odd width at 1x (689): vpX -7848, vpo 0.5, c 2048.5: K = 16 * 345 -
       16 * 2048.5 - 8 = -27264, 0 mod 16 */
    m = frameMap(689, 512, 1);
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 1, 1), "1x odd width: word 0x%05X", w);
    /* the Full pixel crop at 2x: the texture 1300 (even) or 1301 texels
       wide, vpo 0, vpX = floor((1300 - 16384) / 2) = -7542 either way: K =
       16 * 650.5 - 32 * 2048 - 8 = -55136 = 0 mod 16 */
    m = frameMap(688, 512, 2);
    m.tw = 1300;
    m.cropOx = 38.0f;
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 2, 2), "crop 1300: word 0x%05X", w);
    m.tw = 1301;
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 2, 2), "crop 1301: word 0x%05X", w);
    /* a smaller power of two than the target (a device whose limit is 4096):
       the same centres */
    m = frameMap(688, 512, 1);
    m.extentMax = 4096u;
    w = rd_vu_zero_word(&m, &why);
    CHECK(w == mkWord(0, 0, 1, 1), "1x, extent 4096: word 0x%05X", w);

    /* the refusals */
    static const struct {
        const char *name;
        int field, want;
    } kNo[] = {{"not exact", 0, RD_VU_ZERO_MAPPING}, {"no half", 1, RD_VU_ZERO_MAPPING},
               {"jitter", 2, RD_VU_ZERO_JITTER},     {"off grid", 3, RD_VU_ZERO_GRID},
               {"2.8125", 4, RD_VU_ZERO_SCALE},      {"16x", 5, RD_VU_ZERO_SCALE},
               {"crop 2.5", 6, RD_VU_ZERO_SCALE},    {"no size", 7, RD_VU_ZERO_MAPPING}};

    for (size_t i = 0; i < sizeof(kNo) / sizeof(kNo[0]); i++) {
        m = frameMap(688, 512, 1);
        switch (kNo[i].field) {
        case 0:
            m.exact = false;
            break;
        case 1:
            m.noHalf = true;
            break;
        case 2:
            m.jitter = true;
            break;
        case 3:
            m.offGrid = true;
            break;
        case 4:
            m.tw = 1935; /* 688 x 2.8125 */
            m.sx = m.sy = 2.8125f;
            break;
        case 5:
            m = frameMap(688, 512, 16);
            break;
        case 6:
            m = frameMap(688, 512, 2);
            m.sx = 2.5f;
            m.cropOx = 10.0f;
            break;
        default:
            m.tw = 0;
            break;
        }
        why = -5;
        w = rd_vu_zero_word(&m, &why);
        CHECK(w == 0u && why == kNo[i].want, "%s: word 0x%05X, why %d (%s), %d expected",
              kNo[i].name, w, why, rd_vu_zero_reason_name(why), kNo[i].want);
    }
}

/* ---------------------------------------------------- the brute force */

/* the place of g (1/16 GS pixels) on the lattice's axis, in sixteenths of a
   texel */
static int64_t uOf(int32_t g, uint32_t word, int axis)
{
    const int64_t k = axis ? (word >> 4) & 15u : word & 15u;
    const int64_t s = axis ? (word >> 12) & 15u : (word >> 8) & 15u;
    return (int64_t)g * s + k;
}

/* the edge function of a -> b at p (doubled area of a, b, p) */
static int64_t edgeF(const int64_t a[2], const int64_t b[2], const int64_t p[2])
{
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
}

/* a top or left edge of a counter-clockwise triangle (y down: a top edge
   runs left, a left edge runs down) */
static bool topLeft(const int64_t a[2], const int64_t b[2])
{
    const int64_t dx = b[0] - a[0], dy = b[1] - a[1];
    return (dy == 0 && dx < 0) || dy > 0;
}

/* the sample points (multiples of 16) the triangle a, b, c (U, V) covers
   under the top-left rule, or (closed) with every point on an edge
   covered: what any fill rule can cover */
static uint32_t covered(int64_t a[2], int64_t b[2], int64_t c[2], bool closed)
{
    int64_t area = edgeF(a, b, c);
    if (area == 0) {
        return 0;
    }
    if (area < 0) {
        int64_t t[2] = {b[0], b[1]};
        b[0] = c[0];
        b[1] = c[1];
        c[0] = t[0];
        c[1] = t[1];
    }
    int64_t lo[2], hi[2];
    for (int i = 0; i < 2; i++) {
        lo[i] = a[i] < b[i] ? a[i] : b[i];
        lo[i] = c[i] < lo[i] ? c[i] : lo[i];
        hi[i] = a[i] > b[i] ? a[i] : b[i];
        hi[i] = c[i] > hi[i] ? c[i] : hi[i];
    }
    uint32_t n = 0;
    for (int64_t y = (lo[1] + 15) / 16 * 16 - 16; y <= hi[1]; y += 16) {
        for (int64_t x = (lo[0] + 15) / 16 * 16 - 16; x <= hi[0]; x += 16) {
            if (x < lo[0] || y < lo[1]) {
                continue;
            }
            const int64_t p[2] = {x, y};
            const int64_t e0 = edgeF(a, b, p), e1 = edgeF(b, c, p), e2 = edgeF(c, a, p);
            const bool in = closed ? e0 >= 0 && e1 >= 0 && e2 >= 0
                                   : (e0 > 0 || (e0 == 0 && topLeft(a, b))) &&
                                         (e1 > 0 || (e1 == 0 && topLeft(b, c))) &&
                                         (e2 > 0 || (e2 == 0 && topLeft(c, a)));
            n += in ? 1u : 0u;
        }
    }
    return n;
}

static uint32_t s_rng = 12345u;

static uint32_t rnd(uint32_t n)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) % n;
}

static void t2(void)
{
    static const uint32_t kWords[3] = {0x11100u, 0x12280u, 0x13305u};
    for (int wi = 0; wi < 3; wi++) {
        const uint32_t word = kWords[wi];
        uint32_t culled = 0, keptEmpty = 0, wrong = 0, coverCulled = 0, tested = 0;
        for (int i = 0; i < 3000; i++) {
            /* corners within a few texels of GS 2048 */
            int32_t g[3][2];
            const int32_t bx = 32768 + (int32_t)rnd(64), by = 32768 + (int32_t)rnd(64);
            const int32_t r = i % 3 == 0 ? 6 : (i % 3 == 1 ? 20 : 48);
            for (int k = 0; k < 3; k++) {
                g[k][0] = bx + (int32_t)rnd((uint32_t)r);
                g[k][1] = by + (int32_t)rnd((uint32_t)r);
            }
            const int zero =
                rd_vu_zero_test(g[0][0], g[0][1], g[1][0], g[1][1], g[2][0], g[2][1], word);
            tested++;
            int64_t a[2], b[2], c[2];
            a[0] = uOf(g[0][0], word, 0);
            a[1] = uOf(g[0][1], word, 1);
            b[0] = uOf(g[1][0], word, 0);
            b[1] = uOf(g[1][1], word, 1);
            c[0] = uOf(g[2][0], word, 0);
            c[1] = uOf(g[2][1], word, 1);
            int64_t a0[2] = {a[0], a[1]}, b0[2] = {b[0], b[1]}, c0[2] = {c[0], c[1]};
            const uint32_t plain = covered(a0, b0, c0, false);
            if (zero) {
                culled++;
                coverCulled += plain != 0u;
                /* every corner moved by -1, 0, 1 sixteenth on each axis */
                for (int mv = 0; mv < 729; mv++) {
                    int d[6], q = mv;
                    for (int k = 0; k < 6; k++) {
                        d[k] = q % 3 - 1;
                        q /= 3;
                    }
                    int64_t pa[2] = {a[0] + d[0], a[1] + d[1]};
                    int64_t pb[2] = {b[0] + d[2], b[1] + d[3]};
                    int64_t pc[2] = {c[0] + d[4], c[1] + d[5]};
                    if (covered(pa, pb, pc, true) != 0u) {
                        wrong++;
                        break;
                    }
                }
            } else if (plain == 0u) {
                keptEmpty++;
            }
        }
        printf("  lattice 0x%05X: %u of %u culled, %u kept that cover nothing, %u wrong\n", word,
               culled, tested, keptEmpty, wrong);
        CHECK(wrong == 0u, "lattice 0x%05X: %u culled triangles cover a sample point when moved",
              word, wrong);
        CHECK(coverCulled == 0u, "lattice 0x%05X: %u culled triangles cover a point unmoved", word,
              coverCulled);
        CHECK(culled > 100u, "lattice 0x%05X: only %u culled", word, culled);
        CHECK(keptEmpty > 0u, "lattice 0x%05X: no triangle kept by the margin", word);
    }
}

static void t3(void)
{
    /* 1x: sample points at multiples of 16 (a texel is 16 sixteenths of a GS
       pixel); S = (32768, 32768) */
    const uint32_t word = 0x11100u;
    const int32_t S = 32768;

    static const struct {
        const char *name;
        int32_t c[3][2];
        int want;
    } kCase[] = {
        {"inside", {{5, 5}, {11, 5}, {5, 11}}, 1},
        {"inside, the other winding", {{5, 5}, {5, 11}, {11, 5}}, 1},
        {"corner on a point", {{0, 0}, {8, 3}, {3, 8}}, 0},
        {"edge through a point", {{-6, -6}, {6, 6}, {-6, 6}}, 0},
        {"no area, between", {{3, 8}, {8, 8}, {13, 8}}, 1},
        {"no area, through a point", {{-5, 0}, {0, 0}, {5, 0}}, 0},
        {"big", {{-32, -32}, {32, -32}, {-32, 32}}, 0},
        {"within the margin", {{1, 1}, {10, 2}, {2, 10}}, 0},
        {"sliver too wide", {{5, -3}, {101, 1}, {5, 4}}, 0},
        {"sliver between the rows", {{5, 5}, {101, 6}, {5, 8}}, 1},
        {"two texels apart, between", {{20, 20}, {28, 20}, {20, 28}}, 1},
    };

    for (size_t i = 0; i < sizeof(kCase) / sizeof(kCase[0]); i++) {
        const int z =
            rd_vu_zero_test(S + kCase[i].c[0][0], S + kCase[i].c[0][1], S + kCase[i].c[1][0],
                            S + kCase[i].c[1][1], S + kCase[i].c[2][0], S + kCase[i].c[2][1], word);
        CHECK(z == kCase[i].want, "%s: %s, %s expected", kCase[i].name, z ? "culled" : "kept",
              kCase[i].want ? "culled" : "kept");
    }
    /* the target's left edge at 1x, 688 wide: GS 1704 (g 27264), its first
       texel's centre there */
    CHECK(rd_vu_zero_test(27264 + 5, S + 5, 27264 + 11, S + 5, 27264 + 5, S + 11, word) == 1,
          "the left edge's inside case kept");
    CHECK(rd_vu_zero_test(27264, S, 27264 + 8, S + 3, 27264 + 3, S + 8, word) == 0,
          "the left edge's corner case culled");
    /* the half line (ky 8): the same corner case is between the rows */
    CHECK(rd_vu_zero_test(S, S, S + 8, S + 3, S + 3, S + 7, mkWord(0, 8, 1, 1)) == 1,
          "a corner on a GS integer under the half line kept");
}

static uint32_t fbitsU(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void t4(void)
{
    /* a strip of 6 vertices, 4 slots, clip mode none at 1x: slot 0 (v0 v1
       v2) inside a cell, slot 1 (v1 v2 v3) big, slots 2, 3 whatever the
       CPU says */
    const uint32_t word = 0x11100u;
    static const int32_t g[6][2] = {{32773, 32773}, {32779, 32773}, {32773, 32779},
                                    {32900, 32900}, {32700, 32910}, {32705, 32905}};
    uint32_t r1[6 * 4], r2[6 * 4], a0[4 * 4];
    memset(r1, 0, sizeof(r1));
    memset(r2, 0, sizeof(r2));
    memset(a0, 0, sizeof(a0));
    for (int v = 0; v < 6; v++) {
        r1[v * 4 + 0] = fbitsU((float)g[v][0] / 16.0f);
        r1[v * 4 + 1] = fbitsU((float)g[v][1] / 16.0f);
        r2[v * 4 + 3] = 2u | 1u; /* gsOk, inside */
    }
    int want[4];
    for (int j = 0; j < 4; j++) {
        want[j] = rd_vu_zero_test(g[j][0], g[j][1], g[j + 1][0], g[j + 1][1], g[j + 2][0],
                                  g[j + 2][1], word);
        a0[j * 4] = 8192u /* WRAP */ | 4096u /* GSPOS */ | (want[j] ? 131072u : 0u);
    }
    CHECK(want[0] == 1 && want[1] == 0, "the strip's slots: %d %d", want[0], want[1]);
    RdVuZeroCheck c;
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_NONE, word, &c);
    CHECK(c.slots == 4u && c.eligible == 4u && c.mismatches == 0u && c.culled >= 1u,
          "agreeing record: %llu slots, %llu eligible, %llu culled, %llu differ (%s)",
          (unsigned long long)c.slots, (unsigned long long)c.eligible, (unsigned long long)c.culled,
          (unsigned long long)c.mismatches, c.first);
    /* a flag flipped */
    a0[1 * 4] ^= 131072u;
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_NONE, word, &c);
    CHECK(c.mismatches == 1u && strstr(c.first, "triangle slot 1: the GPU culled it, the CPU "
                                                "keeps it") != NULL,
          "flipped: %llu differ (%s)", (unsigned long long)c.mismatches, c.first);
    a0[1 * 4] ^= 131072u;
    /* a flag where no word was given */
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_NONE, 0u, &c);
    CHECK(c.mismatches == (uint64_t)(want[0] + want[1] + want[2] + want[3]) && c.eligible == 0u,
          "no word: %llu differ", (unsigned long long)c.mismatches);
    /* vertex 0 without a GS position: slot 0 untested, its flag a mismatch */
    r2[3] = 1u;
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_NONE, word, &c);
    CHECK(c.mismatches == 1u && c.eligible == 3u, "no GS position: %llu differ, %llu eligible",
          (unsigned long long)c.mismatches, (unsigned long long)c.eligible);
    r2[3] = 3u;
    /* the region mode: EXACT (rule bits 0) tested, CLIP (1) not */
    for (int j = 0; j < 4; j++) {
        a0[j * 4] = (want[j] ? 131072u : 0u) | (j == 3 ? 1u : 0u);
    }
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_REGION, word, &c);
    CHECK(c.eligible == 3u && c.mismatches == (uint64_t)want[3],
          "region: %llu eligible, %llu differ", (unsigned long long)c.eligible,
          (unsigned long long)c.mismatches);
    /* the scissor mode: code 1 in bits 2..3 tested */
    for (int j = 0; j < 4; j++) {
        a0[j * 4] = (want[j] ? 131072u : 0u) | (1u << 2);
    }
    memset(&c, 0, sizeof(c));
    rd_vu_zero_check(a0, r1, r2, 4, ICO_VU_CLIP_SCISSOR, word, &c);
    CHECK(c.eligible == 4u && c.mismatches == 0u, "scissor: %llu eligible, %llu differ",
          (unsigned long long)c.eligible, (unsigned long long)c.mismatches);
}

static void t5(void)
{
    CHECK(rd_vu_zero_ftoi4(1.03125f) == 16, "ftoi4(1.03125) %d", rd_vu_zero_ftoi4(1.03125f));
    CHECK(rd_vu_zero_ftoi4(-1.03125f) == -16, "ftoi4(-1.03125) %d", rd_vu_zero_ftoi4(-1.03125f));
    CHECK(rd_vu_zero_ftoi4(2048.0625f) == 32769, "ftoi4(2048.0625) %d",
          rd_vu_zero_ftoi4(2048.0625f));
    CHECK(rd_vu_zero_ftoi4(1e12f) == INT32_MAX, "ftoi4(1e12)");
    CHECK(rd_vu_zero_ftoi4(-1e12f) == INT32_MIN, "ftoi4(-1e12)");
    CHECK(rd_vu_zero_ftoi4(nanf("")) == INT32_MIN, "ftoi4(NaN)");
}

static void t6(void)
{
    char buf[512];
    putEnv("ICO_RD_VU_ZERO", NULL);
    putEnv("ICO_LOSS_ROW", NULL);
    putEnv("ICO_RD_VU_ZERO_SABOTAGE", NULL);
    /* held until the self-test, on when it passes */
    rd__vu_zero_init();
    CHECK(!rd__vu_zero_on() && !rd__vu_zero_copyable(), "held: on %d, the record copyable",
          rd__vu_zero_on());
    (void)rd_vu_zero_text(buf, sizeof(buf), 1);
    CHECK(strcmp(buf, "; zero-area cull held (until its start-up self-test passes)") == 0,
          "held text: %s", buf);
    rd__vu_zero_verdict(true, "");
    CHECK(rd__vu_zero_on(), "the self-test passed: not on");
    /* =0: off, a passed self-test does not turn it on */
    putEnv("ICO_RD_VU_ZERO", "0");
    rd__vu_zero_init();
    CHECK(!rd__vu_zero_on() && !rd__vu_zero_copyable(), "=0: on %d", rd__vu_zero_on());
    rd__vu_zero_verdict(true, "");
    CHECK(!rd__vu_zero_on(), "=0: on after a passed self-test");
    /* =1: on without the self-test; the block clause */
    putEnv("ICO_RD_VU_ZERO", "1");
    rd__vu_zero_init();
    CHECK(rd__vu_zero_on(), "=1: off");
    rd__vu_zero_draw(true, -1, 100);
    rd__vu_zero_draw(true, -1, 50);
    rd__vu_zero_draw(false, RD_VU_ZERO_VELOCITY, 30);
    rd__vu_zero_draw(false, RD_VU_ZERO_VELOCITY, 30);
    rd__vu_zero_shared_given_up();
    rd__vu_zero_shared_given_up();
    (void)rd_vu_zero_text(buf, sizeof(buf), 2);
    CHECK(strcmp(buf, "; zero-area cull: 1 draws a picture culling, 75 triangles; refused a "
                      "picture: velocity 1; shared lists given up a picture: 1") == 0,
          "=1 text: %s", buf);
    (void)rd_vu_zero_text(buf, sizeof(buf), 2);
    CHECK(strcmp(buf, "; zero-area cull: 0 draws a picture culling, 0 triangles") == 0,
          "the block reset: %s", buf);
    /* no module made without a device (and none before a try) */
    CHECK(rd__vu_zero_variants_made() == 0u, "%u zero-area modules made without a device",
          rd__vu_zero_variants_made());
    /* the words a velocity, skinned or probe entry gives up */
    int kw = -1;
    CHECK(rd__vu_zero_entry_keep(0x11100u, false, true, false, &kw) == 0u &&
              kw == RD_VU_ZERO_VELOCITY,
          "a velocity entry kept its word (why %d)", kw);
    CHECK(rd__vu_zero_entry_keep(0x11100u, true, false, false, &kw) == 0u && kw == RD_VU_ZERO_ENTRY,
          "a skinned entry kept its word");
    CHECK(rd__vu_zero_entry_keep(0x11100u, false, false, false, &kw) == 0x11100u,
          "a plain entry lost its word");
    CHECK(rd__vu_zero_rule_vs(0u) == rd__vu_zero_rule_vs(0x11100u),
          "one rule stage id for every entry (no device: the build without the cull)");
    char small[24];
    const size_t n = rd_vu_zero_text(small, sizeof(small), 1);
    CHECK(n == sizeof(small) - 1u && strlen(small) == sizeof(small) - 1u, "short buffer: %zu", n);
    /* the loss ladder over =1 */
    putEnv("ICO_LOSS_ROW", "1");
    rd__vu_zero_init();
    CHECK(!rd__vu_zero_on() && !rd__vu_zero_copyable(), "the ladder: on");
    (void)rd_vu_zero_text(buf, sizeof(buf), 1);
    CHECK(strstr(buf, "; zero-area cull off (for this start (the loss ladder: 1 start in a row "
                      "before this one lost the graphics device or hung its driver, "
                      "ICO_LOSS_ROW 1") != NULL,
          "the ladder's text: %s", buf);
    rd__vu_zero_verdict(true, "");
    CHECK(!rd__vu_zero_on(), "the ladder: on after a passed self-test");
    putEnv("ICO_LOSS_ROW", NULL);
    /* the install's mark (port/platform/vu_zero_mark.h) over =1 */
    putEnv("ICO_VU_ZERO_BLOCK", "a run of v9.9.9 ended while it was tried");
    rd__vu_zero_init();
    (void)rd_vu_zero_text(buf, sizeof(buf), 1);
    CHECK(!rd__vu_zero_on() && !rd__vu_zero_copyable() &&
              strcmp(buf, "; zero-area cull off (on this install (a run of v9.9.9 ended while it "
                          "was tried))") == 0,
          "the install's mark: %s", buf);
    putEnv("ICO_VU_ZERO_BLOCK", NULL);
    putEnv("ICO_RD_VU_ZERO", NULL);
    /* a failed self-test: off for the run, through a renderer made again */
    rd__vu_zero_init();
    rd__vu_zero_verdict(false, "the pictures differ with the cull, the first at texel 1,2");
    static const char kOff[] = "; zero-area cull off (for the run (its self-test: the pictures "
                               "differ with the cull, the first at texel 1,2))";
    (void)rd_vu_zero_text(buf, sizeof(buf), 1);
    CHECK(strcmp(buf, kOff) == 0, "failed self-test text: %s", buf);
    putEnv("ICO_RD_VU_ZERO", "1");
    rd__vu_zero_init();
    (void)rd_vu_zero_text(buf, sizeof(buf), 1);
    CHECK(!rd__vu_zero_on() && strcmp(buf, kOff) == 0, "the run's off lost at a new renderer: %s",
          buf);
    putEnv("ICO_RD_VU_ZERO", NULL);
    for (int i = 0; i < RD_VU_ZERO_REASONS; i++) {
        CHECK(strcmp(rd_vu_zero_reason_name(i), "?") != 0, "reason %d has no name", i);
    }
    CHECK(strcmp(rd_vu_zero_reason_name(RD_VU_ZERO_REASONS), "?") == 0, "past the reasons");
}

int main(void)
{
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    if (s_fail) {
        printf("rd_vu_zero_test: %d failures\n", s_fail);
        return 1;
    }
    printf("rd_vu_zero_test: ok\n");
    return 0;
}

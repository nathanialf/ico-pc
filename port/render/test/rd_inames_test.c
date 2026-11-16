/* rd_inames_test.c: the draws left at their tick's position, by name, and
 * the stair-steppers (rd_inames.h; the owner's "slow down on movement of
 * objects like the cage").  CPU only:
 *   tally   names by reason, a long name cut to the record's 24 bytes, the
 *           table full counted, the top five of each reason, the line word
 *           for word, a short buffer cut and terminated, an unmatched
 *           name's texture scroll beside its count;
 *   stairs  an object moved every other tick steps at each still tick after
 *           a move and alternates from its fourth judged tick; one moved
 *           every tick, and a still one, never; one that stops counts once;
 *           a key drawn twice in a tick is judged once; a tick skipped
 *           (paired with an older frame) is not judged and breaks the run;
 *           the cadence summed by name and zeroed by the window; the table
 *           emptied when three quarters full; the line's cadence brackets;
 *   moved   the tick before's model rows carried to this tick's view: the
 *           camera's motion cancels, a model moved by 2 units moves, the
 *           bound at 2^-17 of the row's larger lane (1 at 131072: 1 apart
 *           still, 2 apart moved), the GS Z lane left out; a view that does
 *           not invert gives no carry; the kept carry the same. */
#include <stdio.h>
#include <string.h>

#include "rd_inames.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static RdInameTally s_t;

static void test_tally(void)
{
    char b[2048];

    rd_inames_reset(&s_t);
    rd_inames_add(&s_t, "cage", RD_INAME_UNMATCHED, 5);
    rd_inames_add(&s_t, "ico part 2", RD_INAME_UNMATCHED, 2);
    rd_inames_add(&s_t, "ico part 2", RD_INAME_UNMATCHED, 1);
    rd_inames_add(&s_t, "a screen prim", RD_INAME_UNMATCHED, 1);
    rd_inames_add(&s_t, "cage", RD_INAME_JUMPED, 2);
    rd_inames_add(&s_t, "a screen prim", RD_INAME_UNKEYED, 700);
    rd_inames_add(&s_t, "cage part 2", RD_INAME_STAIR, 15);
    rd_inames_add(&s_t, "cage", RD_INAME_COUNT, 3); /* out of range: nothing */
    rd_inames_add(&s_t, "cage", RD_INAME_TURN, 0);  /* none */
    (void)rd_inames_text(&s_t, 250, 348, NULL, b, sizeof b);
    CHECK_STR(
        b,
        "interp names: draws left at their tick's position over 250 frames (348 "
        "presents; counted at each frame's first present): unmatched 9 (cage 5, ico part 2 3, a screen prim 1); mismatched 0; "
        "jumped 2 (cage 2); turn snapped 0; unkeyed 700 (a screen prim 700); "
        "stair-steppers (matched, still this tick after moving the tick before) 15 "
        "(cage part 2 15); names past the table 0");
    /* a short buffer: cut and terminated, the whole length returned */
    {
        char s[20];
        const int n = rd_inames_text(&s_t, 250, 348, NULL, s, sizeof s);

        CHECK(n == (int)strlen(b) && strlen(s) == 19u && strncmp(s, b, 19) == 0);
    }
    /* issue 123's right wall: an unmatched name whose draws scrolled their
       texture says so beside its count; a name not in the table, nothing */
    rd_inames_scroll(&s_t, "cage", 2);
    rd_inames_scroll(&s_t, "cage", 1);
    rd_inames_scroll(&s_t, "4a_gate_sp_cc", 1);
    (void)rd_inames_text(&s_t, 250, 348, NULL, b, sizeof b);
    CHECK(strstr(b, "unmatched 9 (cage 5 [scroll 3], ico part 2 3, a screen prim 1); mismatched 0; "
                    "jumped 2 (cage 2); ") != NULL);
    /* the top five, the most first; a NULL or empty name "a draw" */
    rd_inames_reset(&s_t);
    for (unsigned i = 0; i < 7u; i++) {
        char name[8];

        (void)snprintf(name, sizeof name, "m%u", i);
        rd_inames_add(&s_t, name, RD_INAME_MISMATCHED, i + 1u);
    }
    rd_inames_add(&s_t, NULL, RD_INAME_TURN, 1);
    rd_inames_add(&s_t, "", RD_INAME_TURN, 1);
    (void)rd_inames_text(&s_t, 250, 300, NULL, b, sizeof b);
    CHECK_STR(
        b,
        "interp names: draws left at their tick's position over 250 frames (300 "
        "presents; counted at each frame's first present): unmatched 0; mismatched 28 (m6 7, m5 6, m4 5, m3 4, m2 3); jumped "
        "0; turn snapped 2 (a draw 2); unkeyed 0; stair-steppers (matched, still this "
        "tick after moving the tick before) 0; names past the table 0");
    /* a name of 24 bytes and more is the record's first 23 */
    rd_inames_reset(&s_t);
    rd_inames_add(&s_t, "abcdefghijklmnopqrstuvwxyz", RD_INAME_JUMPED, 1);
    rd_inames_add(&s_t, "abcdefghijklmnopqrstuvwXYZ", RD_INAME_JUMPED, 1);
    (void)rd_inames_text(&s_t, 1, 1, NULL, b, sizeof b);
    CHECK(strstr(b, "jumped 2 (abcdefghijklmnopqrstuvw 2)") != NULL);
    /* the table full: the names past it counted */
    rd_inames_reset(&s_t);
    for (unsigned i = 0; i < RD_INAMES_SLOTS + 3u; i++) {
        char name[16];

        (void)snprintf(name, sizeof name, "part %u", i);
        rd_inames_add(&s_t, name, RD_INAME_UNKEYED, 2);
    }
    CHECK(s_t.total[RD_INAME_UNKEYED] == 2u * (RD_INAMES_SLOTS + 3u) && s_t.lost == 6u);
    (void)rd_inames_text(&s_t, 1, 1, NULL, b, sizeof b);
    CHECK(strstr(b, "; names past the table 6") != NULL);
}

static RdInameStairs s_s;

/* the verdicts of one key over ticks 1..n, moved at tick t when moved(t) */
static unsigned count_bits(uint64_t key, uint32_t hash, uint32_t n, bool (*moved)(uint32_t),
                           unsigned bit)
{
    unsigned got = 0;

    for (uint32_t t = 1; t <= n; t++) {
        got += (rd_inames_stair(&s_s, key, hash, t, t - 1u, moved(t)) & bit) ? 1u : 0u;
    }
    return got;
}

static bool even(uint32_t t)
{
    return (t % 2u) == 0u;
}

static bool always(uint32_t t)
{
    (void)t;
    return true;
}

static bool never(uint32_t t)
{
    (void)t;
    return false;
}

static bool first5(uint32_t t)
{
    return t <= 5u;
}

static void test_stairs(void)
{
    uint32_t j, m, a;
    const uint32_t hs = rd_inames_name_hash("chain4");

    /* moved every other tick: tick 1 the first sight; 2..10 judged, moved at
       2, 4, 6, 8, 10; a step at each still tick after a move (3, 5, 7, 9);
       alternating from the fourth judged tick (5: moved, still, moved, still)
       to 10 */
    rd_inames_stairs_reset(&s_s);
    CHECK(count_bits(0x1234, hs, 10, even, RD_INAMES_V_STEP) == 4u);
    rd_inames_cadence(&s_s, hs, &j, &m, &a);
    CHECK(j == 9u && m == 5u && a == 6u);
    rd_inames_stairs_reset(&s_s);
    CHECK(count_bits(0x1234, hs, 10, even, RD_INAMES_V_ALT) == 6u);
    rd_inames_stairs_reset(&s_s);
    CHECK(count_bits(0x1234, hs, 10, even, RD_INAMES_V_JUDGED) == 9u);
    /* two keys of one name summed; the window zeroes the counts, keeps the keys */
    CHECK(count_bits(0x4321, hs, 10, even, RD_INAMES_V_STEP) == 4u);
    rd_inames_cadence(&s_s, hs, &j, &m, &a);
    CHECK(j == 18u && m == 10u && a == 12u);
    rd_inames_stairs_window(&s_s);
    rd_inames_cadence(&s_s, hs, &j, &m, &a);
    CHECK(j == 0u && m == 0u && a == 0u && s_s.used == 2u);
    /* moved every tick, and still every tick: no step, no alternation */
    rd_inames_stairs_reset(&s_s);
    CHECK(count_bits(0x5678, 1, 10, always, RD_INAMES_V_STEP | RD_INAMES_V_ALT) == 0u);
    CHECK(count_bits(0x9abc, 2, 10, never, RD_INAMES_V_STEP | RD_INAMES_V_ALT) == 0u);
    rd_inames_cadence(&s_s, 1, &j, &m, &a);
    CHECK(j == 9u && m == 9u && a == 0u);
    /* moved for 5 ticks then stopped: once */
    CHECK(count_bits(0xdef0, 3, 10, first5, RD_INAMES_V_STEP) == 1u);
    /* a key drawn twice in a tick: one verdict */
    rd_inames_stairs_reset(&s_s);
    CHECK(rd_inames_stair(&s_s, 7, 0, 19, 18, true) == 0u); /* first sight */
    CHECK(rd_inames_stair(&s_s, 7, 0, 20, 19, true) == (RD_INAMES_V_JUDGED | RD_INAMES_V_MOVED));
    CHECK(rd_inames_stair(&s_s, 7, 0, 21, 20, false) == (RD_INAMES_V_JUDGED | RD_INAMES_V_STEP));
    CHECK(rd_inames_stair(&s_s, 7, 0, 21, 20, false) == 0u);
    /* a tick skipped: the pair is not the tick before's: not judged, the run
       broken (the next still tick is no step) */
    CHECK(rd_inames_stair(&s_s, 8, 0, 30, 29, true) == 0u);
    CHECK(rd_inames_stair(&s_s, 8, 0, 31, 30, true) == (RD_INAMES_V_JUDGED | RD_INAMES_V_MOVED));
    CHECK(rd_inames_stair(&s_s, 8, 0, 33, 32, true) == 0u);
    CHECK(rd_inames_stair(&s_s, 8, 0, 34, 33, false) == RD_INAMES_V_JUDGED);
    /* tick 0: nothing */
    CHECK(rd_inames_stair(&s_s, 9, 0, 0, 0, false) == 0u);
    /* three quarters full: emptied, the next key the first */
    rd_inames_stairs_reset(&s_s);
    for (uint32_t i = 0; i < RD_INAMES_KEYS * 3u / 4u; i++) {
        (void)rd_inames_stair(&s_s, 1000u + i, 0, 5, 4, true);
    }
    CHECK(s_s.used == RD_INAMES_KEYS * 3u / 4u);
    (void)rd_inames_stair(&s_s, 1, 0, 5, 4, true);
    CHECK(s_s.used == 1u);
    /* the line's cadence: the stair names with their keys' counts */
    {
        char b[1024];
        const uint32_t hc = rd_inames_name_hash("cage part 2");

        rd_inames_stairs_reset(&s_s);
        rd_inames_reset(&s_t);
        (void)count_bits(0x77, hc, 10, even, 0u);
        rd_inames_add(&s_t, "cage part 2", RD_INAME_STAIR, 4);
        (void)rd_inames_text(&s_t, 250, 250, &s_s, b, sizeof b);
        CHECK(strstr(b, "; stair-steppers (matched, still this tick after moving the tick "
                        "before) 4 (cage part 2 4 [moved 5 of 9, alternating 6]); names past the "
                        "table 0") != NULL);
        /* NULL and "" are "a draw"'s hash; a long name hashed as cut */
        CHECK(rd_inames_name_hash(NULL) == rd_inames_name_hash("a draw") &&
              rd_inames_name_hash("") == rd_inames_name_hash("a draw"));
        CHECK(rd_inames_name_hash("abcdefghijklmnopqrstuvwxyz") ==
              rd_inames_name_hash("abcdefghijklmnopqrstuvw"));
    }
}

/* a VU block: the frame's view a translation by (tx, ty, tz) (rows 4..7, a
   point's screen place sum_k p_k row k), the model's axes at 16..18 and
   its origin (ox, oy, oz) through the view at 19 */
static void block(float m[36][4], float tx, float ty, float tz, float ox, float oy, float oz)
{
    memset(m, 0, 36 * 4 * sizeof(float));
    m[4][0] = 1.0f;
    m[5][1] = 1.0f;
    m[6][2] = 1.0f;
    m[7][0] = tx;
    m[7][1] = ty;
    m[7][2] = tz;
    m[7][3] = 1.0f;
    m[16][0] = 1.0f;
    m[17][1] = 1.0f;
    m[18][2] = 1.0f;
    m[19][0] = ox + tx;
    m[19][1] = oy + ty;
    m[19][2] = oz + tz;
    m[19][3] = 1.0f;
}

static void test_moved(void)
{
    float a[36][4], c[36][4];
    RdInameCarry k;

    /* the same place, the camera moved: still */
    memset(&k, 0, sizeof(k));
    block(a, 10.0f, -20.0f, 300.0f, 100.0f, 50.0f, -25.0f);
    block(c, 14.0f, -16.0f, 290.0f, 100.0f, 50.0f, -25.0f);
    CHECK(rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
    CHECK(!rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    /* the model moved 2 units: moved (2 against 110 x 2^-17) */
    block(c, 14.0f, -16.0f, 290.0f, 102.0f, 50.0f, -25.0f);
    CHECK(rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
    CHECK(rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    /* the bound: the camera still, X at 131072 (the row's larger lane):
       1 apart is within 131072 x 2^-17 = 1, 2 apart is not */
    block(a, 0.0f, 0.0f, 0.0f, 131072.0f, 0.0f, 0.0f);
    block(c, 0.0f, 0.0f, 0.0f, 131073.0f, 0.0f, 0.0f);
    CHECK(rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
    CHECK(!rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    block(c, 0.0f, 0.0f, 0.0f, 131074.0f, 0.0f, 0.0f);
    CHECK(rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    /* the GS Z lane left out: Z far apart, still */
    block(c, 0.0f, 0.0f, 0.0f, 131072.0f, 0.0f, 9000.0f);
    CHECK(!rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    /* an axis row turned: moved */
    block(c, 0.0f, 0.0f, 0.0f, 131072.0f, 0.0f, 0.0f);
    c[16][0] = 0.0f;
    c[16][1] = 1.0f;
    CHECK(rd_inames_moved(k.t, (const float (*)[4])a, (const float (*)[4])c));
    /* the kept carry: the same views, the same t; a view that does not
       invert gives none */
    {
        double t0[16];

        block(a, 10.0f, -20.0f, 300.0f, 100.0f, 50.0f, -25.0f);
        block(c, 14.0f, -16.0f, 290.0f, 100.0f, 50.0f, -25.0f);
        CHECK(rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
        memcpy(t0, k.t, sizeof(t0));
        CHECK(t0[12] == 4.0 && t0[13] == 4.0 && t0[14] == -10.0 && t0[15] == 1.0);
        CHECK(rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
        CHECK(memcmp(t0, k.t, sizeof(t0)) == 0);
        memset(a, 0, sizeof(a));
        CHECK(!rd_inames_carry(&k, (const float (*)[4])a, (const float (*)[4])c));
    }
}

int main(void)
{
    test_tally();
    test_stairs();
    test_moved();
    if (failures) {
        printf("rd_inames_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rd_inames_test: ok\n");
    return 0;
}

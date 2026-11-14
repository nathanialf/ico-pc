/* rd_inames_test.c: the draws left at their tick's position, by name, and
 * the stair-steppers (rd_inames.h; the owner's "slow down on movement of
 * objects like the cage").  CPU only:
 *   tally   names by reason, a long name cut to the record's 24 bytes, the
 *           table full counted, the top five of each reason, the line word
 *           for word, a short buffer cut and terminated;
 *   stairs  an object moved every other tick steps at each still tick after
 *           a move; one moved every tick, and a still one, never; one that
 *           stops counts once; a key drawn twice in a tick is judged once; a
 *           tick skipped (paired with an older frame) is not judged; the
 *           table emptied when three quarters full;
 *   origin  the model's origin through the inverse of the frame's view: the
 *           camera's motion cancels, a view that does not invert gives none;
 *           the kept inverse gives the same; the moved test's bound (a
 *           millionth of the size). */
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
    (void)rd_inames_text(&s_t, 250, 348, b, sizeof b);
    CHECK_STR(b, "interp names: draws left at their tick's position over 250 frames (348 "
                 "presents): unmatched 9 (cage 5, ico part 2 3, a screen prim 1); mismatched 0; "
                 "jumped 2 (cage 2); turn snapped 0; unkeyed 700 (a screen prim 700); "
                 "stair-steppers (matched, still this tick after moving the tick before) 15 "
                 "(cage part 2 15); names past the table 0");
    /* a short buffer: cut and terminated, the whole length returned */
    {
        char s[20];
        const int n = rd_inames_text(&s_t, 250, 348, s, sizeof s);

        CHECK(n == (int)strlen(b) && strlen(s) == 19u && strncmp(s, b, 19) == 0);
    }
    /* the top five, the most first; a NULL or empty name "a draw" */
    rd_inames_reset(&s_t);
    for (unsigned i = 0; i < 7u; i++) {
        char name[8];

        (void)snprintf(name, sizeof name, "m%u", i);
        rd_inames_add(&s_t, name, RD_INAME_MISMATCHED, i + 1u);
    }
    rd_inames_add(&s_t, NULL, RD_INAME_TURN, 1);
    rd_inames_add(&s_t, "", RD_INAME_TURN, 1);
    (void)rd_inames_text(&s_t, 250, 300, b, sizeof b);
    CHECK_STR(b, "interp names: draws left at their tick's position over 250 frames (300 "
                 "presents): unmatched 0; mismatched 28 (m6 7, m5 6, m4 5, m3 4, m2 3); jumped "
                 "0; turn snapped 2 (a draw 2); unkeyed 0; stair-steppers (matched, still this "
                 "tick after moving the tick before) 0; names past the table 0");
    /* a name of 24 bytes and more is the record's first 23 */
    rd_inames_reset(&s_t);
    rd_inames_add(&s_t, "abcdefghijklmnopqrstuvwxyz", RD_INAME_JUMPED, 1);
    rd_inames_add(&s_t, "abcdefghijklmnopqrstuvwXYZ", RD_INAME_JUMPED, 1);
    (void)rd_inames_text(&s_t, 1, 1, b, sizeof b);
    CHECK(strstr(b, "jumped 2 (abcdefghijklmnopqrstuvw 2)") != NULL);
    /* the table full: the names past it counted */
    rd_inames_reset(&s_t);
    for (unsigned i = 0; i < RD_INAMES_SLOTS + 3u; i++) {
        char name[16];

        (void)snprintf(name, sizeof name, "part %u", i);
        rd_inames_add(&s_t, name, RD_INAME_UNKEYED, 2);
    }
    CHECK(s_t.total[RD_INAME_UNKEYED] == 2u * (RD_INAMES_SLOTS + 3u) && s_t.lost == 6u);
    (void)rd_inames_text(&s_t, 1, 1, b, sizeof b);
    CHECK(strstr(b, "; names past the table 6") != NULL);
}

static RdInameStairs s_s;

static void test_stairs(void)
{
    unsigned steps = 0;

    /* moved every other tick: ticks 2, 4, 6, 8, 10 moved against the one
       before, 3, 5, 7, 9 still: a step at each of those four */
    rd_inames_stairs_reset(&s_s);
    for (uint32_t t = 1; t <= 10u; t++) {
        steps += rd_inames_stair(&s_s, 0x1234, t, t - 1u, (t % 2u) == 0u) ? 1u : 0u;
    }
    CHECK(steps == 4u);
    /* moved every tick, and still every tick: never */
    steps = 0;
    for (uint32_t t = 1; t <= 10u; t++) {
        steps += rd_inames_stair(&s_s, 0x5678, t, t - 1u, true) ? 1u : 0u;
        steps += rd_inames_stair(&s_s, 0x9abc, t, t - 1u, false) ? 1u : 0u;
    }
    CHECK(steps == 0u);
    /* moved for 5 ticks then stopped: once */
    steps = 0;
    for (uint32_t t = 1; t <= 10u; t++) {
        steps += rd_inames_stair(&s_s, 0xdef0, t, t - 1u, t <= 5u) ? 1u : 0u;
    }
    CHECK(steps == 1u);
    /* a key drawn twice in a tick: one verdict */
    rd_inames_stairs_reset(&s_s);
    CHECK(!rd_inames_stair(&s_s, 7, 20, 19, true));
    CHECK(rd_inames_stair(&s_s, 7, 21, 20, false));
    CHECK(!rd_inames_stair(&s_s, 7, 21, 20, false));
    /* a tick skipped: the pair is not the tick before's: not judged */
    CHECK(!rd_inames_stair(&s_s, 8, 30, 29, true));
    CHECK(!rd_inames_stair(&s_s, 8, 32, 31, false));
    /* tick 0: nothing */
    CHECK(!rd_inames_stair(&s_s, 9, 0, 0, false));
    /* three quarters full: emptied, the next key the first */
    rd_inames_stairs_reset(&s_s);
    for (uint32_t i = 0; i < RD_INAMES_KEYS * 3u / 4u; i++) {
        (void)rd_inames_stair(&s_s, 1000u + i, 5, 4, true);
    }
    CHECK(s_s.used == RD_INAMES_KEYS * 3u / 4u);
    (void)rd_inames_stair(&s_s, 1, 5, 4, true);
    CHECK(s_s.used == 1u);
}

/* a VU block: the frame's view a translation by (tx, ty, tz) at 4..7
   (column-major), the model's origin (ox, oy, oz) through it at 19 */
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
    m[19][0] = ox + tx;
    m[19][1] = oy + ty;
    m[19][2] = oz + tz;
    m[19][3] = 1.0f;
}

static void test_origin(void)
{
    float a[36][4], c[36][4], oa[3], oc[3];
    RdInameView k;

    /* the same place, the camera moved: not moved */
    block(a, 10.0f, -20.0f, 300.0f, 100.0f, 50.0f, -25.0f);
    block(c, 14.0f, -16.0f, 290.0f, 100.0f, 50.0f, -25.0f);
    CHECK(rd_inames_origin((const float (*)[4])a, oa) &&
          rd_inames_origin((const float (*)[4])c, oc));
    CHECK(oa[0] == 100.0f && oa[1] == 50.0f && oa[2] == -25.0f);
    CHECK(!rd_inames_moved(oa, oc));
    /* the model moved 2 units, the camera still: moved */
    block(c, 10.0f, -20.0f, 300.0f, 102.0f, 50.0f, -25.0f);
    CHECK(rd_inames_origin((const float (*)[4])c, oc) && oc[0] == 102.0f);
    CHECK(rd_inames_moved(oa, oc));
    /* the kept inverse: the same answers, the view inverted again on a change */
    memset(&k, 0, sizeof(k));
    CHECK(rd_inames_origin_kept(&k, (const float (*)[4])a, oa) && oa[0] == 100.0f);
    CHECK(rd_inames_origin_kept(&k, (const float (*)[4])c, oc) && oc[0] == 102.0f);
    block(c, 14.0f, -16.0f, 290.0f, 100.0f, 50.0f, -25.0f);
    CHECK(rd_inames_origin_kept(&k, (const float (*)[4])c, oc) && oc[0] == 100.0f &&
          oc[1] == 50.0f);
    /* a view that does not invert */
    memset(a, 0, sizeof(a));
    CHECK(!rd_inames_origin((const float (*)[4])a, oa));
    memset(&k, 0, sizeof(k));
    CHECK(!rd_inames_origin_kept(&k, (const float (*)[4])a, oa));
    /* the bound: a millionth of the size, 1 at least */
    {
        const float p[3] = {1000.0f, 0.0f, 0.0f};
        const float q[3] = {1000.0005f, 0.0f, 0.0f}; /* 1000.000488 */
        const float r[3] = {1000.002f, 0.0f, 0.0f};  /* 1000.001953 */
        const float s[3] = {0.0f, 0.0f, 0.0f};
        const float u[3] = {0.0f, 0.0000005f, 0.0f};
        const float v[3] = {0.0f, 0.000002f, 0.0f};

        CHECK(!rd_inames_moved(p, q) && rd_inames_moved(p, r));
        CHECK(!rd_inames_moved(s, u) && rd_inames_moved(s, v));
    }
}

int main(void)
{
    test_tally();
    test_stairs();
    test_origin();
    if (failures) {
        printf("rd_inames_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rd_inames_test: ok\n");
    return 0;
}

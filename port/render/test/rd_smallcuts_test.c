/* rd_smallcuts_test.c: the pure parts of v0.11.6's small cuts (CPU only).
 *   rd__turn_rect (rd_turn_fold.c): every pixel of a rect turned by the
 *     rect's map is where rd_prerotate.c's table puts that pixel (the
 *     twin's read: 1 (X, Y) = (h - 1 - y, x), 2 (w - 1 - x, h - 1 - y),
 *     3 (y, w - 1 - x)), for every quarter turn, at the phone's window and
 *     at odd sizes, the rects at the corners and inside
 *   rd__turn_fold_why: each fact keeps the turn pass with its reason, in
 *     the order the present checks them; the box blit alone folds
 *   rd__vu_class_pick, rd__vu_class_measured (rd_vu_class.c): the PowerVR
 *     class's rule through the attempt ledger's decider: on by default on
 *     the measured PowerVR (the D-Series DXT-48-1536) alone, off by default
 *     on the rest of the class, on by the tests' switch, never where a mode
 *     is asked by name or the vertex stores are not auto's pick */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_turn_fold.h"
#include "rd_vu_class.h"
#include "rd_vu_record.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* rd_prerotate.c's table: the image pixel of the picture's (x, y) */
static void tablePixel(uint32_t q, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t *X,
                       uint32_t *Y)
{
    if (q == 1u) {
        *X = h - 1u - y;
        *Y = x;
    } else if (q == 2u) {
        *X = w - 1u - x;
        *Y = h - 1u - y;
    } else if (q == 3u) {
        *X = y;
        *Y = w - 1u - x;
    } else {
        *X = x;
        *Y = y;
    }
}

static void testRect(uint32_t w, uint32_t h, RhiRect r)
{
    for (uint32_t q = 0; q < 4u; q++) {
        const RhiRect t = rd__turn_rect(q, w, h, r);
        const uint32_t iw = (q & 1u) ? h : w, ih = (q & 1u) ? w : h;
        CHECK(t.w == ((q & 1u) ? r.h : r.w) && t.h == ((q & 1u) ? r.w : r.h));
        CHECK(t.x >= 0 && t.y >= 0 && (uint32_t)t.x + t.w <= iw && (uint32_t)t.y + t.h <= ih);
        /* every pixel of r lands inside t, and t holds nothing else (the
           sizes match, so landing inside for each is a bijection) */
        for (uint32_t y = (uint32_t)r.y; y < (uint32_t)r.y + r.h; y++) {
            for (uint32_t x = (uint32_t)r.x; x < (uint32_t)r.x + r.w; x++) {
                uint32_t X, Y;
                tablePixel(q, w, h, x, y, &X, &Y);
                if (X < (uint32_t)t.x || X >= (uint32_t)t.x + t.w || Y < (uint32_t)t.y ||
                    Y >= (uint32_t)t.y + t.h) {
                    printf("FAIL turn %u of %ux%u rect %d,%d %ux%u: pixel %u,%u at %u,%u outside "
                           "%d,%d %ux%u\n",
                           q, w, h, r.x, r.y, r.w, r.h, x, y, X, Y, t.x, t.y, t.w, t.h);
                    failures++;
                    return;
                }
            }
        }
    }
}

static void testTurnRect(void)
{
    /* the phone's window (2364x1080) with its box the whole window, a 4:3
       box with bars, the border mask's inset; odd sizes; corners */
    testRect(2364, 1080, (RhiRect){0, 0, 2364, 1080});
    testRect(2364, 1080, (RhiRect){282, 0, 1800, 1080});
    testRect(2364, 1080, (RhiRect){290, 8, 1784, 1064});
    testRect(48, 30, (RhiRect){7, 5, 11, 7});
    testRect(48, 30, (RhiRect){29, 17, 11, 7});
    testRect(48, 30, (RhiRect){0, 0, 1, 1});
    testRect(48, 30, (RhiRect){47, 29, 1, 1});
    testRect(7, 3, (RhiRect){2, 1, 5, 2});
    /* the self-test's own case, by value: 90 degrees of 7,5 11x7 in 48x30
       is 18,7 7x11 */
    const RhiRect t = rd__turn_rect(1u, 48, 30, (RhiRect){7, 5, 11, 7});
    CHECK(t.x == 18 && t.y == 7 && t.w == 7u && t.h == 11u);
}

static void testWhy(void)
{
    const RdTurnFoldPresent alone = {false, false, false, true, 0u, false, true};
    CHECK(rd__turn_fold_why(&alone) == RD_TF_FOLD);
    RdTurnFoldPresent p = alone;
    p.batches = 3;
    CHECK(rd__turn_fold_why(&p) == RD_TF_OVERLAY);
    p = alone;
    p.oneToOne = false;
    CHECK(rd__turn_fold_why(&p) == RD_TF_SCALED);
    p = alone;
    p.capture = true;
    CHECK(rd__turn_fold_why(&p) == RD_TF_CAPTURE);
    p = alone;
    p.formatOk = false;
    CHECK(rd__turn_fold_why(&p) == RD_TF_FORMAT);
    p = alone;
    p.scene = true;
    CHECK(rd__turn_fold_why(&p) == RD_TF_SCENE);
    p = alone;
    p.depth = true;
    CHECK(rd__turn_fold_why(&p) == RD_TF_DEPTH);
    /* the CRT filter first, whatever else holds */
    const RdTurnFoldPresent all = {true, true, true, false, 9u, true, false};
    CHECK(rd__turn_fold_why(&all) == RD_TF_CRT);
    CHECK(strcmp(rd__turn_fold_words(RD_TF_OVERLAY),
                 "the port's overlay or the deferred text drawn") == 0);
    CHECK(strcmp(rd__turn_fold_words(RD_TF_COUNT), "?") == 0);
}

static void testVuClass(void)
{
    ico_attempt_reset();
    /* the measured PowerVR's name, and the class's others */
    CHECK(rd__vu_class_measured("PowerVR D-Series DXT-48-1536 MC1"));
    CHECK(!rd__vu_class_measured("PowerVR B-Series BXM-8-256"));
    CHECK(!rd__vu_class_measured("PowerVR Rogue GE8320"));
    CHECK(!rd__vu_class_measured(NULL));
    /* the owner's phone (the DXT, both stores), the define 0: off */
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, true, true, -1, false));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    /* the build's default (1): on there */
    CHECK(rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, true, true, -1,
                            RD_VU_COMPUTE_CLASS_DEFAULT != 0) ==
          (RD_VU_COMPUTE_CLASS_DEFAULT != 0));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) ==
          (RD_VU_COMPUTE_CLASS_DEFAULT ? ICO_ATT_ON : ICO_ATT_OFF_DEFAULT));
    /* another PowerVR (never ran it), the default on: off by default, the
       vertex stores, with the reason */
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, true, false, -1, true));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(strstr(ico_attempt_why_of(ICO_ATT_VU_COMPUTE_CLASS),
                 "not the PowerVR D-Series DXT-48-1536 it was shown on") != NULL);
    /* the tests' switch on lavapipe (not the class): on */
    CHECK(rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, false, false, 1, false));
    /* =0 on the measured PowerVR with the default on: off */
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, true, true, 0, true));
    /* a mode asked by name, or no vertex stores (the Mali: auto's compute
       already), or no compute stores: not applicable */
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_STORES, RD_VU_RECORD_STORES, true, true, true, 1, true));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    CHECK(
        !rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_COMPUTE, true, false, false, -1, true));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, false, true, true, 1, true));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    /* another class, the default on: not applicable */
    CHECK(!rd__vu_class_pick(RD_VU_RECORD_AUTO, RD_VU_RECORD_STORES, true, false, false, -1, true));
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    ico_attempt_reset();
}

int main(void)
{
    testTurnRect();
    testWhy();
    testVuClass();
    if (failures) {
        printf("rd_smallcuts_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_smallcuts_test: ok\n");
    return 0;
}

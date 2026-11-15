/* shadow_clip_test.c: the box the shadow volumes' edges are clipped to, by
 * disc and frame size (port/include/ico_shadow_clip.h; Shadow.c
 * clipVolumeEdge reads it through GsBase.c gsb_HostShadowClipX / Y).  Pure:
 * no game code, no device.
 *
 * The expectations are each disc's own code, about the screen centre with
 * ScreenWidth 512 and ScreenHeight 448 or 512 (GsBase.c gsb_Init):
 *
 *   own      the European, Japanese, Korean and Asian discs clip at
 *            +-ScreenWidth by +-ScreenHeight (512 by 448, 512 by 512); the
 *            North American disc and the two 2001 previews at +-(ScreenWidth
 *            * 3 / 2) by +-(ScreenHeight * 3 / 2) (768 by 672, 768 by 768).
 *            At the 4:3 frame (512 across) the box is the disc's own to the
 *            pixel.
 *   widths   at every native width (512 .. 2560 in steps of 16,
 *            ico_frame_width_for's values): across the width times 1 or 3 /
 *            2, at most ICO_SHADOW_CLIP_X_MAX; never less than the disc's
 *            own 4:3 box, never less than the width capped the same way (the
 *            box of every disc before the discs' own were followed), past
 *            the picture's side (W / 2), not narrower than at the width
 *            before, and a clipped end packs into Shadow.c's range test
 *            (0x11 .. 0xFFEF sixteenths about 2048) on both axes; down the
 *            disc's own at every width.
 *   aspects  the widths of the listed aspects through ico_frame_width_for:
 *            16:9 is 688 across (688 and 1032), 32:9 1376 (1376 and the cap:
 *            2064 is past it), 48:9 2048 (the cap on every disc).
 *   rows     a number that is no row of config/regions.txt takes the
 *            European disc's box; the three-halves set is exactly the North
 *            American disc and the two previews. */
#include "../../include/ico_frame.h"
#include "../../include/ico_shadow_clip.h"
#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the PS2's frame: the width every disc's own box is a multiple of */
#define PS2_W 512

static const struct {
    int region;
    const char *name;
    int num, den; /* the disc's own box over the frame */
} kDisc[] = {
    {ICO_REGION_PAL, "the European disc", 1, 1},
    {ICO_REGION_JPN, "the Japanese disc", 1, 1},
    {ICO_REGION_KOR, "the Korean disc", 1, 1},
    {ICO_REGION_NTSC, "the North American disc", 3, 2},
    {ICO_REGION_AUG6, "the August 2001 preview", 3, 2},
    {ICO_REGION_JUN26, "the June 2001 demo", 3, 2},
    {ICO_REGION_CN, "the Asian disc", 1, 1},
};

#define DISCS ((int)(sizeof kDisc / sizeof kDisc[0]))

static const int kHeight[2] = {448, 512};

/* a clipped end at 2048 -+ c, as emitVolumeStrip packs it (sixteenths),
   passes volumeVertsOutOfRange */
static int packs(float c)
{
    const int lo = (int)((2048.0f - c) * 16.0f), hi = (int)((2048.0f + c) * 16.0f);

    return lo >= 0x11 && hi <= 0xFFEF;
}

static void testOwn(void)
{
    CHECK(DISCS == ICO_REGION_COUNT, "%d discs listed, config/regions.txt has %d", DISCS,
          ICO_REGION_COUNT);
    for (int d = 0; d < DISCS; d++) {
        const int r = kDisc[d].region;
        const float x = ico_shadow_clip_x(r, PS2_W);

        CHECK(r == d, "%s is row %d, listed at %d", kDisc[d].name, r, d);
        CHECK(x == (float)(PS2_W * kDisc[d].num / kDisc[d].den),
              "own: %s at 4:3 clips across at %g, its own code at %d", kDisc[d].name, (double)x,
              PS2_W * kDisc[d].num / kDisc[d].den);
        for (int h = 0; h < 2; h++) {
            const float y = ico_shadow_clip_y(r, kHeight[h]);

            CHECK(y == (float)(kHeight[h] * kDisc[d].num / kDisc[d].den),
                  "own: %s at height %d clips down at %g, its own code at %d", kDisc[d].name,
                  kHeight[h], (double)y, kHeight[h] * kDisc[d].num / kDisc[d].den);
        }
    }
    /* the numbers themselves */
    CHECK(ico_shadow_clip_x(ICO_REGION_PAL, 512) == 512.0f &&
              ico_shadow_clip_y(ICO_REGION_PAL, 448) == 448.0f &&
              ico_shadow_clip_y(ICO_REGION_PAL, 512) == 512.0f,
          "own: the European disc's box is not 512 by 448 / 512");
    CHECK(ico_shadow_clip_x(ICO_REGION_NTSC, 512) == 768.0f &&
              ico_shadow_clip_y(ICO_REGION_NTSC, 448) == 672.0f &&
              ico_shadow_clip_y(ICO_REGION_NTSC, 512) == 768.0f,
          "own: the North American disc's box is not 768 by 672 / 768");
}

static void testWidths(void)
{
    for (int d = 0; d < DISCS; d++) {
        const int r = kDisc[d].region;
        float before = 0.0f;

        for (int w = PS2_W; w <= ICO_FRAME_W_MAX; w += 16) {
            const float x = ico_shadow_clip_x(r, w);
            const float full = (float)(w * kDisc[d].num / kDisc[d].den);
            const float was = (float)w > ICO_SHADOW_CLIP_X_MAX ? ICO_SHADOW_CLIP_X_MAX : (float)w;
            const float own = (float)(PS2_W * kDisc[d].num / kDisc[d].den);

            CHECK(x == (full > ICO_SHADOW_CLIP_X_MAX ? ICO_SHADOW_CLIP_X_MAX : full),
                  "widths: %s at %d clips across at %g, want %g capped at %g", kDisc[d].name, w,
                  (double)x, (double)full, (double)ICO_SHADOW_CLIP_X_MAX);
            CHECK(x >= own, "widths: %s at %d clips across at %g, inside its own %g", kDisc[d].name,
                  w, (double)x, (double)own);
            CHECK(x >= was, "widths: %s at %d clips across at %g, inside the frame's %g",
                  kDisc[d].name, w, (double)x, (double)was);
            CHECK(x > (float)w * 0.5f, "widths: %s at %d clips across at %g, inside the picture",
                  kDisc[d].name, w, (double)x);
            CHECK(x >= before, "widths: %s at %d clips across at %g, less than at %d (%g)",
                  kDisc[d].name, w, (double)x, w - 16, (double)before);
            CHECK(packs(x), "widths: %s at %d: an end clipped at %g fails the range test",
                  kDisc[d].name, w, (double)x);
            before = x;
        }
        for (int h = 0; h < 2; h++) {
            const float y = ico_shadow_clip_y(r, kHeight[h]);

            CHECK(y >= (float)kHeight[h] && packs(y), "widths: %s at height %d clips down at %g",
                  kDisc[d].name, kHeight[h], (double)y);
        }
    }
    /* the cap itself: 2046 packs, 2047 does not (GS x 1 and 4095) */
    CHECK(packs(ICO_SHADOW_CLIP_X_MAX) && !packs(ICO_SHADOW_CLIP_X_MAX + 1.0f),
          "widths: the cap %g and the range test", (double)ICO_SHADOW_CLIP_X_MAX);
}

static void testAspects(void)
{
    const int w169 = ico_frame_width_for(16.0f / 9.0f);
    const int w329 = ico_frame_width_for(32.0f / 9.0f);
    const int w489 = ico_frame_width_for(48.0f / 9.0f);

    CHECK(ico_frame_width_for(4.0f / 3.0f) == 512 && w169 == 688 && w329 == 1376 && w489 == 2048,
          "aspects: the widths %d, %d, %d", w169, w329, w489);
    CHECK(ico_shadow_clip_x(ICO_REGION_PAL, w169) == 688.0f &&
              ico_shadow_clip_x(ICO_REGION_NTSC, w169) == 1032.0f,
          "aspects: 16:9 clips at %g and %g", (double)ico_shadow_clip_x(ICO_REGION_PAL, w169),
          (double)ico_shadow_clip_x(ICO_REGION_NTSC, w169));
    CHECK(ico_shadow_clip_x(ICO_REGION_PAL, w329) == 1376.0f &&
              ico_shadow_clip_x(ICO_REGION_NTSC, w329) == ICO_SHADOW_CLIP_X_MAX,
          "aspects: 32:9 clips at %g and %g", (double)ico_shadow_clip_x(ICO_REGION_PAL, w329),
          (double)ico_shadow_clip_x(ICO_REGION_NTSC, w329));
    CHECK(ico_shadow_clip_x(ICO_REGION_PAL, w489) == ICO_SHADOW_CLIP_X_MAX &&
              ico_shadow_clip_x(ICO_REGION_NTSC, w489) == ICO_SHADOW_CLIP_X_MAX,
          "aspects: 48:9 clips at %g and %g", (double)ico_shadow_clip_x(ICO_REGION_PAL, w489),
          (double)ico_shadow_clip_x(ICO_REGION_NTSC, w489));
}

static void testRows(void)
{
    static const int kNoRow[3] = {-1, ICO_REGION_COUNT, 1000};

    for (int i = 0; i < 3; i++) {
        CHECK(!ico_shadow_clip_three_halves(kNoRow[i]) &&
                  ico_shadow_clip_x(kNoRow[i], 512) == 512.0f &&
                  ico_shadow_clip_y(kNoRow[i], 448) == 448.0f,
              "rows: %d is no row and does not take the European disc's box", kNoRow[i]);
    }
    for (int r = 0; r < ICO_REGION_COUNT; r++) {
        const int want = r == ICO_REGION_NTSC || r == ICO_REGION_AUG6 || r == ICO_REGION_JUN26;

        CHECK(ico_shadow_clip_three_halves(r) == want, "rows: row %d three halves %d, want %d", r,
              ico_shadow_clip_three_halves(r), want);
    }
}

int main(void)
{
    testOwn();
    testWidths();
    testAspects();
    testRows();
    if (failures) {
        printf("shadow_clip_test: %d failures\n", failures);
        return 1;
    }
    printf("shadow_clip_test: ok\n");
    return 0;
}

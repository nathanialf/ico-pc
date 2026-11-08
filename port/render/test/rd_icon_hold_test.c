/* rd_icon_hold_test.c: the title's last picture held on screen while the
 * icon capture's frames are drawn (rd.h rd_icon_hold, rd_icon_hold.h).
 *
 * Without a device:
 *   rule      rd__icon_hold_tainted: no pass, frames before it, the pass's
 *             frames while it is on and after its off, a blend with its
 *             last frame, the frames past it (title_icon.h
 *             ico_title_icon_present_clean's rule)
 * On a Vulkan device (exit 77 without one), presented to a 1024 x 768
 * headless output (the 4:3 box fills it), frames drawn into DISPLAY in the
 * Original preset (one present a frame): "title" frames (a grey field, a
 * red and a blue sprite at an offset) and "pass" frames (black, one white
 * sprite: the letter alone):
 *   held      the hold on, a title frame presented (rd_icon_hold_ready
 *             then, not before), the pass on: the pass frame's present
 *             shows the title frame's picture byte for byte, and the state
 *             picture armed in that frame (rd_capture_shot) is the pass
 *             frame's picture, the same as one presented with no hold; the
 *             clear turned white, the second pass frame the same; the pass
 *             off, the next title frame presented live (the reference
 *             picture of it, not the held one), and after the release too
 *   black     the hold on and the pass on with no present between: the
 *             pass frame's present is black, not the pass's picture
 *   none      a pass frame without a hold presents as it is drawn
 * Exit 0, 1 on a mismatch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_icon_hold.h"
#include "vk/rhi_vk.h"

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

#define OUT_W 1024
#define OUT_H 768
/* DISPLAY for a 512 x 512 scene: 512 x 256 */
#define DW 512
#define DH 256
#define OUT_BYTES ((size_t)OUT_W * OUT_H * 4)

static const uint8_t kGrey[4] = {0x50, 0x48, 0x40, 0x80}, kRed[4] = {220, 30, 10, 0x80},
                     kBlue[4] = {10, 40, 230, 0x80}, kWhiteV[4] = {0xFF, 0xFF, 0xFF, 0x80},
                     kBlackV[4] = {0, 0, 0, 0x80};
/* the pass's clear colours (title_icon.c) */
static const uint8_t kBlack[4] = {0, 0, 0, 0x80}, kWhite[4] = {0xFF, 0xFF, 0xFF, 0x80};

static uint8_t *s_title10, *s_title20, *s_pass, *s_out;

static void testRule(void)
{
    const uint32_t N = RD_ICON_HOLD_NONE;
    CHECK(!rd__icon_hold_tainted(5, 1.0f, N, N), "rule: no pass, nothing held");
    CHECK(!rd__icon_hold_tainted(9, 1.0f, 10, N) && !rd__icon_hold_tainted(9, 0.5f, 10, N),
          "rule: a frame before the pass's first is shown");
    CHECK(rd__icon_hold_tainted(10, 1.0f, 10, N) && rd__icon_hold_tainted(10, 0.25f, 10, N),
          "rule: the pass's first frame, whole or blended with the one before, is held");
    CHECK(rd__icon_hold_tainted(40, 1.0f, 10, N), "rule: any frame while the pass is on is held");
    CHECK(rd__icon_hold_tainted(20, 1.0f, 10, 20) && rd__icon_hold_tainted(15, 1.0f, 10, 20),
          "rule: the pass's frames after its off are held");
    CHECK(rd__icon_hold_tainted(21, 0.5f, 10, 20) && rd__icon_hold_tainted(21, 0.0f, 10, 20),
          "rule: a blend with the pass's last frame is held");
    CHECK(!rd__icon_hold_tainted(21, 1.0f, 10, 20) && !rd__icon_hold_tainted(22, 0.25f, 10, 20),
          "rule: the frames past the pass are shown");
}

static RdScreenVtx vtx(int32_t x, int32_t y, const uint8_t c[4])
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = x;
    v.y = y;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* a sprite (x0, y0)..(x1, y1) in DISPLAY pixels */
static void sprite(int x0, int y0, int x1, int y1, const uint8_t c[4])
{
    const int32_t ox = (2048 - DW / 2) * 16, oy = (2048 - DH / 2) * 16;
    RdScreenVtx v[2] = {vtx(ox + x0 * 16, oy + y0 * 16, c), vtx(ox + x1 * 16, oy + y1 * 16, c)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

static void opaque2D(void)
{
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
}

typedef struct ShotSeen {
    int calls;
    uint8_t *px;
    uint32_t w, h;
} ShotSeen;

static ShotSeen s_shot;

static void shotFn(uint8_t *rgba, uint32_t w, uint32_t h, void *user)
{
    ShotSeen *seen = (ShotSeen *)user;
    seen->calls++;
    free(seen->px);
    seen->px = rgba;
    seen->w = w;
    seen->h = h;
}

static void shotForget(void)
{
    free(s_shot.px);
    memset(&s_shot, 0, sizeof(s_shot));
}

/* one frame into DISPLAY, presented by rd_end_frame: the title (dx its
   sprites' offset) or the pass's picture; shot: the state picture armed
   in the open frame */
static void frameOf(int pass, int dx, int shot)
{
    rd_begin_frame();
    if (shot) {
        CHECK(rd_capture_shot(shotFn, &s_shot), "the state picture armed in the open frame");
    }
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_DISPLAY), (RdTarget){0}, DW, DH, 0);
    rd_clear_target(rd_target(RD_TARGET_DISPLAY), pass ? kBlackV : kGrey, 0, 0);
    opaque2D();
    if (pass) {
        sprite(200, 60, 240, 200, kWhiteV);
    } else {
        sprite(20 + dx, 30, 120 + dx, 90, kRed);
        sprite(300, 100 + dx, 380, 180 + dx, kBlue);
    }
    rd_end_frame(0);
}

static int readOut(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    if (!rd_read_presented(dst, &w, &h) || w != OUT_W || h != OUT_H) {
        CHECK(0, "present readback (%u x %u)", w, h);
        return 0;
    }
    return 1;
}

static size_t differ(const uint8_t *a, const uint8_t *b)
{
    size_t n = 0;
    for (size_t i = 0; i < (size_t)OUT_W * OUT_H; i++) {
        n += memcmp(a + i * 4, b + i * 4, 3) != 0;
    }
    return n;
}

static size_t lit(const uint8_t *a)
{
    size_t n = 0;
    for (size_t i = 0; i < (size_t)OUT_W * OUT_H; i++) {
        n += (a[i * 4] | a[i * 4 + 1] | a[i * 4 + 2]) != 0;
    }
    return n;
}

/* the state picture against a whole presented picture, in the box */
static size_t shotDiffer(const uint8_t *want)
{
    uint32_t ow = 0, oh = 0;
    RhiRect box;
    memset(&box, 0, sizeof(box));
    if (!rd__last_present_box(&ow, &oh, &box) || !s_shot.px || s_shot.w != box.w ||
        s_shot.h != box.h || ow != OUT_W || oh != OUT_H) {
        CHECK(0, "the state picture: %ux%u for the box %ux%u at %d,%d", s_shot.w, s_shot.h, box.w,
              box.h, box.x, box.y);
        return (size_t)-1;
    }
    size_t n = 0;
    for (uint32_t y = 0; y < box.h; y++) {
        for (uint32_t x = 0; x < box.w; x++) {
            const uint8_t *a = s_shot.px + ((size_t)y * box.w + x) * 4;
            const uint8_t *b = want + ((size_t)(box.y + y) * OUT_W + (box.x + x)) * 4;
            n += memcmp(a, b, 3) != 0;
        }
    }
    return n;
}

static void testHeld(void)
{
    /* the references: each picture presented with no hold */
    frameOf(1, 0, 0);
    if (!readOut(s_pass)) {
        return;
    }
    frameOf(0, 10, 0);
    if (!readOut(s_title10)) {
        return;
    }
    frameOf(0, 20, 0);
    if (!readOut(s_title20)) {
        return;
    }
    CHECK(differ(s_pass, s_title10) > 10000 && differ(s_title10, s_title20) > 1000,
          "the pass's and the two title pictures differ (%zu, %zu pixels)",
          differ(s_pass, s_title10), differ(s_title10, s_title20));

    /* none: a pass frame without a hold presents as drawn */
    rd_set_icon_pass(kBlack);
    frameOf(1, 0, 0);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_pass) == 0, "none: the pass frame presented as drawn (%zu differ)",
              differ(s_out, s_pass));
    }
    rd_set_icon_pass(NULL);

    /* held */
    rd_icon_hold(true);
    CHECK(!rd_icon_hold_ready(), "held: no picture before a present");
    frameOf(0, 10, 0);
    CHECK(rd_icon_hold_ready(), "held: a picture after the title frame's present");
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title10) == 0, "held: the title frame presented live (%zu differ)",
              differ(s_out, s_title10));
    }
    rd_set_icon_pass(kBlack);
    shotForget();
    frameOf(1, 0, 1);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title10) == 0,
              "held: the pass frame's present shows the title's last picture (%zu differ)",
              differ(s_out, s_title10));
    }
    CHECK(s_shot.calls == 1 && s_shot.px, "held: the state picture taken (%d calls)", s_shot.calls);
    if (s_shot.px) {
        const size_t d = shotDiffer(s_pass);
        CHECK(d == 0, "held: the state picture is the pass frame's picture (%zu differ)", d);
    }
    rd_set_icon_pass(kWhite);
    frameOf(1, 0, 0);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title10) == 0,
              "held: the second pass frame shows the title's last picture (%zu differ)",
              differ(s_out, s_title10));
    }
    rd_set_icon_pass(NULL);
    frameOf(0, 20, 0);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title20) == 0,
              "held: past the pass, the live picture again (%zu differ)", differ(s_out, s_title20));
    }
    rd_icon_hold(false);
    CHECK(!rd_icon_hold_ready(), "held: released");
    frameOf(0, 20, 0);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title20) == 0, "held: after the release, live (%zu differ)",
              differ(s_out, s_title20));
    }
    shotForget();

    /* black: the pass on before any present of the hold */
    rd_icon_hold(true);
    rd_set_icon_pass(kBlack);
    frameOf(1, 0, 0);
    CHECK(!rd_icon_hold_ready(), "black: no picture held");
    if (readOut(s_out)) {
        CHECK(lit(s_out) == 0, "black: the pass frame's present is black (%zu lit pixels)",
              lit(s_out));
    }
    rd_set_icon_pass(NULL);
    frameOf(0, 10, 0);
    if (readOut(s_out)) {
        CHECK(differ(s_out, s_title10) == 0, "black: past the pass, live (%zu differ)",
              differ(s_out, s_title10));
    }
    rd_icon_hold(false);
    frameOf(0, 10, 0);
}

int main(void)
{
    testRule();
    s_title10 = malloc(OUT_BYTES);
    s_title20 = malloc(OUT_BYTES);
    s_pass = malloc(OUT_BYTES);
    s_out = malloc(OUT_BYTES);
    if (!s_title10 || !s_title20 || !s_pass || !s_out) {
        printf("rd_icon_hold_test: out of memory\n");
        return 1;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    int rc = 0;
    if (!rd_init(512, 512, &s, NULL)) {
        printf("rd_icon_hold_test: SKIP: no usable Vulkan device\n");
        rc = failures ? 1 : 77;
    } else {
        rd__set_not_implemented_fatal(false);
        testHeld();
        CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
        const uint32_t verr = rhi_vk_validation_error_count();
        CHECK(verr == 0, "%u validation errors", verr);
        rd_shutdown();
        rc = failures ? 1 : 0;
        if (failures) {
            printf("rd_icon_hold_test: %d failures\n", failures);
        } else {
            printf("rd_icon_hold_test: ok\n");
        }
    }
    free(s_title10);
    free(s_title20);
    free(s_pass);
    free(s_out);
    return rc;
}

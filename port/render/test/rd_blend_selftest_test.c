/* rd_blend_selftest_test.c: the exact blend's self-test
 * (rd_blend_selftest.c) and the attachment read's Qualcomm rule
 * (vk_device.c).
 *
 *   rd_blend_selftest_test <mode> [<scale>]
 *
 * <mode> the blend the self-test must leave in force: attachment, storage
 * or fixed.  On a Vulkan device (77 without one), at a wide scene (16:9,
 * Full pixel, scale 3 unless given):
 *   1. rd_blend_selftest leaves <mode> in force; it returns true unless
 *      ICO_RD_BLEND_SABOTAGE names the mode in force;
 *   2. without the attachment read on the device (ICO_VK_FAKE_ADRENO=1,
 *      ICO_VK_FAKE_NO_DST=1) no target is made for it and no pass is
 *      begun readable; RhiLimits.rasterOrderOff says why under the fake
 *      Adreno;
 *   3. the forest's tint frame (rd_pixel_test.c tintFrame, through the
 *      renderer's own calls) under the mode in force: each cell PCSX2's
 *      pixel under an exact mode, within 1 level under the fixed-function
 *      blend;
 *   4. the scene made at another size runs the self-test again (a second
 *      line) and the mode stays;
 *   5. no validation error.
 * The log lines are matched by test/rd_blend_selftest.cmake. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
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

/* rd_pixel_test.c kTint */
static const struct {
    uint8_t dst[3], pcsx2[3];
} kTint[8] = {
    {{15, 22, 8}, {21, 28, 14}},  {{12, 17, 14}, {18, 23, 19}}, {{37, 40, 31}, {42, 45, 36}},
    {{66, 84, 38}, {70, 88, 43}}, {{23, 31, 16}, {29, 36, 21}}, {{9, 9, 9}, {15, 15, 15}},
    {{79, 89, 56}, {83, 93, 60}}, {{25, 31, 24}, {31, 36, 29}},
};

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

/* WORK0's GS size (its target record) */
static void work0Size(uint32_t *w, uint32_t *h)
{
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_WORK0).id);
    *w = t ? t->w : 512;
    *h = t ? t->h : 256;
}

static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4])
{
    uint32_t w, h;
    work0Size(&w, &h);
    const int32_t ox = (2048 - (int32_t)w / 2) * 16, oy = (2048 - (int32_t)h / 2) * 16;
    RdScreenVtx v[2] = {vtx(ox + x0, oy + y0, c), vtx(ox + x1, oy + y1, c)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
}

/* rd_pixel_test.c tintFrame, on WORK0 at its own size: eight 32 x 32
 * cells, then the tint sprite over the row */
static void tintFrame(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0}, tint[4] = {223, 219, 205, 4};
    uint32_t w, h;
    work0Size(&w, &h);
    rd_begin_frame();
    rd_select_list(4);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, w, h, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    for (int i = 0; i < 8; i++) {
        const uint8_t c[4] = {kTint[i].dst[0], kTint[i].dst[1], kTint[i].dst[2], 0x80};
        sprite(i * 32 * 16, 0, (i * 32 + 32) * 16, 32 * 16, c);
    }
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    sprite(0, 0, 256 * 16, 32 * 16, tint);
    rd_end_frame(0);
}

static void checkTint(bool exact)
{
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_WORK0).id);
    const size_t size = t ? (size_t)t->tw * t->th * 4 : 0;
    uint8_t *buf = size ? malloc(size) : NULL;
    uint32_t w = 0, h = 0;
    if (!buf || !rd__read_target(rd_target(RD_TARGET_WORK0), buf, size, &w, &h)) {
        CHECK(0, "readback of WORK0");
        free(buf);
        return;
    }
    int worst = 0;
    for (int i = 0; i < 8; i++) {
        /* the cell's centre in WORK0's texels */
        const size_t x = (size_t)(i * 32 + 16) * w / t->w, y = (size_t)16 * h / t->h;
        const uint8_t *p = &buf[(y * w + x) * 4];
        for (int ch = 0; ch < 3; ch++) {
            const int d = abs((int)p[ch] - kTint[i].pcsx2[ch]);
            worst = d > worst ? d : worst;
        }
    }
    free(buf);
    printf("  forest tint (%s, WORK0 %ux%u texels): worst %d against PCSX2's\n",
           rd__blend_exact_name(g_rd.dstMode), w, h, worst);
    CHECK(worst <= (exact ? 0 : 1), "forest tint: %d levels off PCSX2's", worst);
}

/* no target made for the attachment read, no pass begun readable */
static void checkNoAttachment(void)
{
    for (int id = 0; id < RD_TARGET_COUNT; id++) {
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)id).id);
        if (!t) {
            continue;
        }
        CHECK((t->dstUsage & 2u) == 0, "target %d made for the attachment read", id);
        CHECK(!rd__pass_wants_readable(t), "target %d's passes begun readable", id);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: rd_blend_selftest_test <attachment|storage|fixed> [<scale>]\n");
        return 2;
    }
    const RdBlendExact want = strcmp(argv[1], "attachment") == 0 ? RD_BLEND_EXACT_ATTACHMENT
                              : strcmp(argv[1], "storage") == 0  ? RD_BLEND_EXACT_STORAGE
                                                                 : RD_BLEND_EXACT_OFF;
    const float scale = argc > 2 ? (float)atof(argv[2]) : 3.0f;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = scale;
    st.fullPixel = 1;
    st.outputWidth = 1280;
    st.outputHeight = 720;
    const uint32_t w = 640, h = 448;
    if (!rd_init(w, h, &st, NULL)) {
        printf("rd_blend_selftest_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    const char *fakeQc = getenv("ICO_VK_FAKE_ADRENO");
    const char *force = getenv("ICO_VK_ATTACHMENT_READ");
    const char *noDst = getenv("ICO_VK_FAKE_NO_DST");
    const char *sab = getenv("ICO_RD_BLEND_SABOTAGE");
    const bool adreno = fakeQc && fakeQc[0] == '1' && !(force && force[0] == '1');
    const bool none = noDst && noDst[0] == '1';
    const RhiLimits *lim = rhi_limits();
    /* ICO_VK_FAKE_LIMITS=moltenvk: the device reports MoltenVK, no
     * attachment read; the interlock is the device's own, so a device
     * without it cannot show the default (it has nothing exact to take
     * away) */
    const char *fakeLim = getenv("ICO_VK_FAKE_LIMITS");
    const bool mvk = fakeLim && strcmp(fakeLim, "moltenvk") == 0;
    if (mvk) {
        CHECK(lim->moltenvk, "RhiLimits.moltenvk under the fake MoltenVK");
        CHECK(!lim->rasterOrderAttachment, "the attachment read offered under the fake MoltenVK");
        if (!lim->fragmentInterlock) {
            printf("rd_blend_selftest_test: SKIP: no fragment shader interlock on this device\n");
            rd_shutdown();
            return 77;
        }
    }

    /* 1. the self-test */
    const bool passed = rd_blend_selftest();
    const RdBlendExact got = rd_blend_exact();
    printf("  self-test %s, %s in force\n", passed ? "passed" : "failed",
           rd__blend_exact_name(got));
    CHECK(got == want, "%s in force, %s expected", rd__blend_exact_name(got),
          rd__blend_exact_name(want));
    const bool sabotaged =
        sab && ((got == RD_BLEND_EXACT_ATTACHMENT && strstr(sab, "attachment")) ||
                (got == RD_BLEND_EXACT_STORAGE && strstr(sab, "storage")));
    CHECK(mvk || passed == !sabotaged, "the self-test %s", passed ? "passed" : "failed");

    /* 2. the attachment read not offered */
    if (adreno) {
        CHECK(!lim->rasterOrderAttachment, "the attachment read offered under the fake Adreno");
        CHECK(lim->rasterOrderOff && strstr(lim->rasterOrderOff, "Qualcomm driver"),
              "RhiLimits.rasterOrderOff: %s", lim->rasterOrderOff ? lim->rasterOrderOff : "(null)");
    } else {
        CHECK(lim->rasterOrderOff == NULL, "RhiLimits.rasterOrderOff set: %s", lim->rasterOrderOff);
    }
    if (adreno || none) {
        CHECK((g_rd.dstModes & (1u << RD_BLEND_EXACT_ATTACHMENT)) == 0,
              "the attachment read among the modes");
        checkNoAttachment();
    }

    /* 3. a frame recorded through the renderer's calls */
    tintFrame();
    checkTint(got != RD_BLEND_EXACT_OFF);

    /* 4. another scene size */
    rd_reset_scene(w + 64, h);
    CHECK(rd_blend_exact() == want, "%s in force after the new size, %s expected",
          rd__blend_exact_name(rd_blend_exact()), rd__blend_exact_name(want));
    tintFrame();
    checkTint(rd_blend_exact() != RD_BLEND_EXACT_OFF);

    /* 5. */
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_blend_selftest_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_blend_selftest_test: ok\n");
    return 0;
}

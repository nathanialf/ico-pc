/* rd_vuprepass_selftest_test.c: the VU vertex and triangle pre-passes'
 * self-test (rd_vuprepass_selftest.c).
 *
 *   rd_vuprepass_selftest_test <on|off|notrun> [plain]
 *
 * <mode> what the self-test must leave: on (it passed), off (it failed:
 * ICO_RD_VU_PREPASS_SABOTAGE=1, tri or ticks) or notrun (the pre-pass
 * already off: ICO_RD_VU_PREPASS=0).  On a Vulkan device (77 without one),
 * at a wide scaled scene (the Enhanced preset, 16:9, Full pixel, scale 2)
 * or, with plain, at the Original preset's 4:3 and 1x:
 *   1. rd_vuprepass_selftest leaves the pre-pass on or off as <mode> says
 *      (g_rd.vuPrepass) and returns false only when it turned it off;
 *   2. the summary text (rd_summary_text) says so: "VU pre-pass on
 *      (self-test: ok)", "VU pre-pass off (self-test: the pictures differ"
 *      or "VU pre-pass off by the settings"; with it on, its draws batched
 *      (g_rd.vuPreBatch) unless ICO_RD_VU_PREPASS_BATCH=0; the draw merge
 *      on (g_rd.vuMerge, "VU merge on") unless ICO_RD_VU_MERGE=0 or on
 *      Direct3D 11, the self-test's pictures drawn through it; and the device profile's
 *      class (rd_profile.c) names the backend, OpenGL and D3D11 in the
 *      desktop column: "immediate-mode GPU (... OpenGL):";
 *   3. the reachable set (rd__enumerate_reachable, what the window makes
 *      after it) holds, with the pre-pass on, the main shaders' keys
 *      (vu_main_vs, vu_particle_main_vs), the vu_X_pre_vs entries' and the
 *      five record stage keys once each (rule, tick R, tick G, planes,
 *      sprite), and no program entry's; with it off none of
 *      the pre-passes' keys;
 *   4. a frame drawn after it starts SCENE from its clear (no pixel of the
 *      self-test's block is left);
 *   5. no validation error.
 * The log lines are matched by test/rd_vuprepass_selftest.cmake. */
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

/* the reachable set's VU keys by kind */
static void checkSet(bool on)
{
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
    uint32_t main = 0, pre = 0, stage = 0, prog = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t vs = keys[i].vs;
        main += vs == RD_VS_VU_MAIN || vs == RD_VS_VU_PARTICLE_MAIN;
        pre += rd__vs_is_vu_pre(vs);
        stage += vs >= RD_VS_VU_TRI_RULE_PRE && vs <= RD_VS_VU_PARTICLE_PRE;
        prog += vs >= RD_VS_VU_FIRST && vs <= RD_VS_VU_LAST;
    }
    printf("  the reachable set: %u keys, %u of the main shaders, %u pre-pass entries, %u record "
           "stages, %u of the programs' entries\n",
           n, main, pre, stage, prog);
    if (on) {
        CHECK(main > 0 && pre == 10 && stage == 5 && prog == 0,
              "with the pre-pass: %u main keys, %u pre-pass keys, %u stage keys (5), %u program "
              "keys",
              main, pre, stage, prog);
    } else {
        CHECK(pre == 0 && stage == 0, "without the pre-pass: %u pre-pass keys, %u stage keys", pre,
              stage);
    }
}

/* a frame of a clear only: the self-test's block of SCENE (256 x 96 GS
 * pixels at its centre, rd_vuprepass_selftest.c blockRect) must be the
 * clear's colour */
static void checkCleared(void)
{
    static const uint8_t grey[4] = {16, 32, 48, 0x80};
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_end_frame(0);
    const size_t size = t ? (size_t)t->tw * t->th * 4 : 0;
    uint8_t *buf = size ? malloc(size) : NULL;
    uint32_t w = 0, h = 0;
    if (!buf || !rd__read_target(rd_target(RD_TARGET_SCENE), buf, size, &w, &h)) {
        CHECK(0, "readback of SCENE");
        free(buf);
        return;
    }
    const double gx0 = (double)((t->w - 256u) / 2), gy0 = (double)((t->h - 96u) / 2);
    size_t other = 0, seen = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            /* the texel's GS pixel (rd__gs_texel_x's mapping) */
            const double gx = t->ox > 0.0f ? (x + t->ox) / t->sx : (double)x * t->w / t->tw;
            const double gy = (double)y * t->h / t->th;
            if (gx < gx0 || gx >= gx0 + 256.0 || gy < gy0 || gy >= gy0 + 96.0) {
                continue;
            }
            seen++;
            other += memcmp(&buf[((size_t)y * w + x) * 4], grey, 3) != 0;
        }
    }
    free(buf);
    CHECK(seen > 0, "no texel of SCENE in the self-test's block");
    CHECK(other == 0, "%zu of %zu texels of the self-test's block not the clear's colour", other,
          seen);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: rd_vuprepass_selftest_test <on|off|notrun> [plain]\n");
        return 2;
    }
    const bool wantOn = strcmp(argv[1], "on") == 0;
    const bool notRun = strcmp(argv[1], "notrun") == 0;
    const bool plain = argc > 2 && strcmp(argv[2], "plain") == 0;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = plain ? RD_PRESET_ORIGINAL : RD_PRESET_ENHANCED;
    st.aspect = plain ? 4.0f / 3.0f : 16.0f / 9.0f;
    st.sceneScale = plain ? 1.0f : 2.0f;
    st.fullPixel = plain ? 0 : 1;
    st.outputWidth = plain ? 640 : 1280;
    st.outputHeight = plain ? 480 : 720;
    if (!rd_init(640, 448, &st, NULL)) {
        printf("rd_vuprepass_selftest_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    rd__set_not_implemented_fatal(true);
    printf("  the VU shaders' %s variant\n", rd__vu_extended() ? "extended" : "plain");

    /* 1. */
    const bool before = g_rd.vuPrepass;
    const bool kept = rd_vuprepass_selftest();
    printf("  the pre-pass %s before, %s after (the self-test returned %s)\n",
           before ? "on" : "off", g_rd.vuPrepass ? "on" : "off", kept ? "true" : "false");
    CHECK(before == !notRun, "the pre-pass %s before the self-test", before ? "on" : "off");
    CHECK(g_rd.vuPrepass == wantOn, "the pre-pass %s after the self-test, %s expected",
          g_rd.vuPrepass ? "on" : "off", wantOn ? "on" : "off");
    CHECK(kept == (wantOn || notRun), "the self-test returned %s", kept ? "true" : "false");

    /* 2. */
    char sum[640];
    rd_summary_text(sum, sizeof(sum));
    printf("  summary: %s\n", sum);
    const char *want = wantOn   ? "; VU pre-pass on (self-test: ok); "
                       : notRun ? "; VU pre-pass off by the settings ("
                                : "; VU pre-pass off (self-test: the pictures differ: ";
    CHECK(strstr(sum, want) != NULL, "the summary lacks \"%s\"", want);
    if (wantOn) {
        /* the pre-pass's draws batched unless ICO_RD_VU_PREPASS_BATCH=0
           (rd_replay_vu.c preBatchInit): the self-test above ran that way */
        const char *b = getenv("ICO_RD_VU_PREPASS_BATCH");
        const bool wantBatch = !(b && strcmp(b, "0") == 0);
        printf("  the pre-pass's draws %s\n", g_rd.vuPreBatch ? "batched" : "one an entry");
        CHECK(g_rd.vuPreBatch == wantBatch, "the pre-pass's draws %s, %s expected",
              g_rd.vuPreBatch ? "batched" : "one an entry", wantBatch ? "batched" : "one an entry");
    }
    {
        /* the draw merge on unless ICO_RD_VU_MERGE=0 or on Direct3D 11
           (rd_replay_vu.c vuMergeInit): the self-test's draws went through it */
        const char *m = getenv("ICO_RD_VU_MERGE");
        const bool wantMerge = !(m && strcmp(m, "0") == 0) && rhi_backend() != RHI_BACKEND_D3D11;
        printf("  the draw merge %s\n", g_rd.vuMerge ? "on" : "off");
        CHECK(g_rd.vuMerge == wantMerge, "the draw merge %s, %s expected",
              g_rd.vuMerge ? "on" : "off", wantMerge ? "on" : "off");
        const char *mw = wantMerge ? "; VU merge on; " : "; VU merge off; ";
        CHECK(strstr(sum, mw) != NULL, "the summary lacks \"%s\"", mw);
    }
    {
        /* the class ends with the backend's name: "(Mesa, Vulkan):",
           "(OpenGL):" (llvmpipe names no vendor gl_device.c knows) */
        char cls[64];
        snprintf(cls, sizeof(cls), "%s):", rhi_backend_kind_name(rhi_backend()));
        CHECK(strstr(rd__profile_text(), cls) != NULL, "the profile \"%s\" lacks \"%s\"",
              rd__profile_text(), cls);
        const bool desktop =
            rhi_backend() == RHI_BACKEND_OPENGL || rhi_backend() == RHI_BACKEND_D3D11;
        CHECK(!desktop || strncmp(rd__profile_text(), "immediate-mode GPU (", 20) == 0,
              "the %s profile is not the desktop column: \"%s\"",
              rhi_backend_kind_name(rhi_backend()), rd__profile_text());
    }

    /* 3. */
    checkSet(wantOn);

    /* 4. */
    checkCleared();

    /* 5. */
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_vuprepass_selftest_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_vuprepass_selftest_test: ok\n");
    return 0;
}

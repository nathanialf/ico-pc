/* rd_stencil_selftest_test.c: the stencil loaded and stored inside the
 * shadow window only (rd_stencil_selftest.c).
 *
 * Run by ctest with ICO_RD_FOG_PATH=local and ICO_RD_STENCIL_PACKED=0
 * (lavapipe is not the PowerVR class: the switch runs the self-test there),
 * rd_stencil_selftest_poison with ICO_RD_STORE_POISON=1 too (a stencil load
 * the cut leaves undefined clears to 0x5A; lavapipe keeps DONT_CARE
 * contents, so without it a wrong cut would not show):
 *   1. CPU: the 10 s clause and the --stats words from a made-up record;
 *   2. on a Vulkan device (77 without one, or without the local read): the
 *      scene at 640x448, Enhanced, scale 2, Full pixel, 16:9 (a SCENE whose
 *      texels are not its GS pixels, as rd_fog_selftest_test's);
 *      rd_fog_selftest passes on the local path, the stencil self-test ran
 *      and passed (its eight texels the GS's and the forced replay's), the
 *      stencil is not forced, and the cut's replay asked no stencil store at
 *      2 passes or more (the new targets' clear of SCENE and list 0's pass,
 *      whose depth later lists read) and no stencil load at 1 or more (list
 *      5's pass, which loads the depth);
 *   3. the decision by path and switch on this device (the desktop class):
 *      =1 forced; unset, the local path forced (not the PowerVR class), the
 *      in-place path not, the copy and buffer paths forced; =0 not forced;
 *   4. no validation error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_stencil_selftest.h"
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

/* "" reads as unset (rd_stencil_selftest.c readEnv) */
static void setEnv(const char *k, const char *v)
{
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

static void cpuLines(void)
{
    /* ctest's ICO_RD_STENCIL_PACKED=0: the cut on whatever the path (the
       10 s clause is printed only then) */
    rd__stencil_cut_path(RD_FOG_LOCAL);
    CHECK(!rd__stencil_cut_forced(), "ICO_RD_STENCIL_PACKED=0 before a device: the cut on");
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.stencilCutStores = 2;
    rec.stencilCutLoads = 1;
    rec.stencilCutStoreTexels = 2u * 1048576u;
    rec.stencilCutLoadTexels = 1048576u;
    rd__stencil_cut_pop(&rec);
    char text[512];
    size_t at = 0;
    text[0] = '\0';
    rd__stencil_cut_text(text, sizeof text, &at);
    printf("  10 s clause: %s\n", text);
    CHECK(strcmp(text, "; stencil: 2.0 stores and 1.0 loads a replay not asked with the depth's "
                       "(2.0/1.0 MB), loaded and stored inside the shadow window only") == 0,
          "the 10 s clause: %s", text);
    CHECK(at == strlen(text), "the clause's length %zu, %zu written", at, strlen(text));
    /* summed and cleared: nothing the next time */
    const size_t was = at;
    rd__stencil_cut_text(text, sizeof text, &at);
    CHECK(at == was, "a second clause without a record");
    char st[256];
    rd__stencil_cut_stats(&rec, st, sizeof st);
    printf("  --stats: %s\n", st);
    CHECK(strstr(st, ": 2 stores and 1 loads not asked with the depth's (2.00 MB stored, 1.00 MB "
                     "loaded)") != NULL,
          "the stats words: %s", st);
}

int main(void)
{
    printf("rd_stencil_selftest_test\n");
    cpuLines();
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 2.0f;
    st.fullPixel = 1;
    st.outputWidth = 1280;
    st.outputHeight = 720;
    if (!rd_init(640, 448, &st, NULL)) {
        printf("rd_stencil_selftest_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    if (!rd__fog_local_offered()) {
        printf("rd_stencil_selftest_test: SKIP: no local read on this device\n");
        rd_shutdown();
        return failures ? 1 : 77;
    }

    /* 2. the self-tests */
    const bool passed = rd_fog_selftest();
    printf("  fog self-test %s, path %s; stencil self-test: %s\n", passed ? "passed" : "failed",
           rd__fog_path_name(g_rd.fogPath), rd__stencil_cut_test_words());
    CHECK(passed, "the fog's self-test passed");
    CHECK(g_rd.fogPath == RD_FOG_LOCAL, "path %s, local expected", rd__fog_path_name(g_rd.fogPath));
    CHECK(rd__stencil_cut_test_ok(), "the stencil self-test passed: %s",
          rd__stencil_cut_test_words());
    CHECK(!rd__stencil_cut_forced(), "the stencil inside the shadow window only");
    CHECK(g_rd.depthCopy, "the depth copy flag as the local path sets it (held off only inside "
                          "the self-test)");
    /* g_rdPerf is the self-test's last replay's record: the cut's */
    printf("  the cut's replay: %u stencil stores and %u loads not asked\n",
           g_rdPerf.stencilCutStores, g_rdPerf.stencilCutLoads);
    CHECK(g_rdPerf.stencilCutStores >= 2u, "%u stencil stores not asked, 2 or more expected",
          g_rdPerf.stencilCutStores);
    CHECK(g_rdPerf.stencilCutLoads >= 1u, "%u stencil loads not asked, 1 or more expected",
          g_rdPerf.stencilCutLoads);
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const uint64_t texels = t ? (uint64_t)t->tw * t->th : 0u;
    /* each counted pass is SCENE's whole (no pass area on the desktop
       class) or its area: at most a SCENE each, at least one texel */
    CHECK(texels > 0u && g_rdPerf.stencilCutStoreTexels >= g_rdPerf.stencilCutStores &&
              g_rdPerf.stencilCutStoreTexels <= (uint64_t)g_rdPerf.stencilCutStores * texels &&
              g_rdPerf.stencilCutLoadTexels <= (uint64_t)g_rdPerf.stencilCutLoads * texels,
          "the texels: %llu stored, %llu loaded for %llu a SCENE",
          (unsigned long long)g_rdPerf.stencilCutStoreTexels,
          (unsigned long long)g_rdPerf.stencilCutLoadTexels, (unsigned long long)texels);

    /* 3. the decision on this device by path and switch */
    CHECK(!rd__profile_powervr(), "lavapipe is not the PowerVR class");
    setEnv("ICO_RD_STENCIL_PACKED", "1");
    rd__stencil_cut_path(RD_FOG_LOCAL);
    CHECK(rd__stencil_cut_forced(), "ICO_RD_STENCIL_PACKED=1: forced");
    setEnv("ICO_RD_STENCIL_PACKED", "");
    rd__stencil_cut_path(RD_FOG_LOCAL);
    CHECK(rd__stencil_cut_forced(), "the local path off the PowerVR class: forced");
    rd__stencil_cut_path(RD_FOG_INPLACE);
    CHECK(!rd__stencil_cut_forced(), "the in-place path: inside the window only");
    rd__stencil_cut_path(RD_FOG_COPY);
    CHECK(rd__stencil_cut_forced(), "the copy path: forced");
    rd__stencil_cut_path(RD_FOG_BUFFER);
    CHECK(rd__stencil_cut_forced(), "the buffer path: forced");
    setEnv("ICO_RD_STENCIL_PACKED", "0");
    rd__stencil_cut_path(g_rd.fogPath);
    CHECK(!rd__stencil_cut_forced(), "ICO_RD_STENCIL_PACKED=0: inside the window only");

    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_stencil_selftest_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_stencil_selftest_test: ok\n");
    return 0;
}

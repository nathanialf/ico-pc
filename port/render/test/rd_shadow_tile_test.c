/* rd_shadow_tile_test.c: the shadow composites over the tile mask
 * (rd_shadow_tile.c).
 *
 *   rd_shadow_tile_test frames    (ctest rd_shadow_tile)
 *   rd_shadow_tile_test startup   (ctest rd_shadow_tile_selftest,
 *                                  ICO_RD_SHADOW_TILE=1)
 *
 * Both with ICO_RD_DATE_PATH=stencil (DATE through the stencil bit, the
 * tiles' path; lavapipe has the local read), 77 without a Vulkan device or
 * without that path.  First the CPU words: the 10 s clause and the --stats
 * line from a made-up record.  Then a 640x448 scene, Enhanced, scale 2,
 * Full pixel, 16:9 (SCENE's texels are not its GS pixels):
 *   frames   the self-test's frames, a shadow in a corner, shadows at the
 *            frame's edges, no shadow, each with the B-spline composites off
 *            and on: SCENE byte for byte with the tiles off and on (a later
 *            DATE DATM 1 sprite reading bit 6 in it), the corner's and the
 *            edges' composite over a mask with dead tiles;
 *   startup  rd_fog_selftest's first run takes the tiles' self-test, and it
 *            passed (rd__shadow_tile_test_ok);
 * no validation error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_shadow_tile.h"
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

static void cpuLines(void)
{
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.shadowTileComposites = 3;
    rec.shadowTileMasks = 3;
    rec.shadowTileWhy[RD_ST_WHY_PASS] = 1;
    rd__shadow_tile_pop(&rec);
    char text[512];
    size_t at = 0;
    text[0] = '\0';
    rd__shadow_tile_text(text, sizeof text, &at);
    printf("  10 s clause: %s\n", text);
    CHECK(strcmp(text, "; shadow tiles: 3.0 composites a replay over the tile mask (3.0 masks), "
                       "1.0 whole (pass open 1.0)") == 0,
          "the 10 s clause: %s", text);
    const size_t was = at;
    rd__shadow_tile_text(text, sizeof text, &at);
    CHECK(at == was, "a second clause without a record");
    char st[512];
    rd__shadow_tile_stats(&rec, st, sizeof st);
    printf("  --stats: %s\n", st);
    CHECK(strstr(st, ": 3 composites over the tile mask (3 masks), 1 whole (pass open 1); tiles "
                     "live none") != NULL,
          "the stats words: %s", st);
}

int main(int argc, char **argv)
{
    printf("rd_shadow_tile_test\n");
    const bool startup = argc > 1 && strcmp(argv[1], "startup") == 0;
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
        printf("rd_shadow_tile_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    if (!g_rd.profile.dateStencil || !g_rd.profile.shadowOne) {
        printf("rd_shadow_tile_test: SKIP: no DATE stencil bit or no one-draw count here\n");
        rd_shutdown();
        return failures ? 1 : 77;
    }
    if (startup) {
        (void)rd_fog_selftest();
        CHECK(rd__shadow_tile_test_ok(), "the start-up self-test passed");
        CHECK(rd__shadow_tile_on(), "the tiles on (ICO_RD_SHADOW_TILE=1)");
    } else {
        static const char *const kNames[3] = {"a corner", "the edges", "no shadow"};
        const bool bspline = g_rd.profile.shadowBspline;
        for (int b = 0; b < 2; b++) {
            g_rd.profile.shadowBspline = b != 0;
            for (int v = 0; v < 3; v++) {
                char words[224];
                const bool ok = rd__shadow_tile_selftest_variant(v, words, sizeof words);
                printf("  %s, B-spline %s: %s\n", kNames[v], b ? "on" : "off", words);
                CHECK(ok, "%s with the B-spline %s: %s", kNames[v], b ? "on" : "off", words);
            }
        }
        g_rd.profile.shadowBspline = bspline;
    }
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_shadow_tile_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_shadow_tile_test: ok\n");
    return 0;
}

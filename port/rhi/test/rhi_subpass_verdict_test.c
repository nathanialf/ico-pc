/* rhi_subpass_verdict_test.c: the subpass read's decision at a Vulkan start
 * and its verdict file (rhi_subpass_verdict.h), no device: every branch of
 * rhi_subpass_decide in its order (the [dev] key first, then the route, the
 * file, the timestamps, the verdict kept for this device and driver), what
 * it asks to be written, the key's text, the file beside the pipeline cache
 * file (both separators, none, too long), the line's parse (another key,
 * another format, an unknown state, a why cut to its buffer, the line's
 * end), and a write, read and remove of a real file in the current folder. */
#include <stdio.h>
#include <string.h>

#include "rhi_subpass_verdict.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct Case {
    int ask, route, fake, file, stamps;
    RhiSubpassClass cls;
    int classEnv;
    RhiSubpassVerdict prev;
    RhiSubpassDecision want;
    RhiSubpassWrite wantWrite;
    const char *wantWhy; /* the words' start */
} Case;

static void test_decide(void)
{
    static const Case k[] = {
        /* the [dev] key and the fake first, whatever else */
        {0, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_KEPT, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "[dev] subpass_read = off"},
        {1, 1, 1, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "ICO_VK_FAKE_NO_DST, over [dev] subpass_read = on"},
        {-1, 1, 1, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "ICO_VK_FAKE_NO_DST"},
        {1, 0, 0, 0, 0, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_REFUSED, RHI_SUBPASS_ON,
         RHI_SUBPASS_WRITE_NOTHING, "[dev] subpass_read = on"},
        /* automatic: the route, the file, the timestamps */
        {-1, 0, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "the device's passes read their attachments by local read"},
        {-1, 1, 0, 0, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "no file to keep the probe's verdict in"},
        {-1, 1, 0, 1, 0, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_KEPT, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "no GPU timestamps"},
        /* the verdict */
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_KEPT, RHI_SUBPASS_ON,
         RHI_SUBPASS_WRITE_NOTHING, "kept by the probe of an earlier start (why)"},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_REFUSED, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "refused by the probe of an earlier start (why)"},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_PROBING, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_REFUSED,
         "refused for good: the start that probed it ended before its verdict"},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_PROBE,
         RHI_SUBPASS_WRITE_PROBING, "no verdict for this device and driver yet"},
        /* the classes' defaults this release: Arm, Qualcomm and the others
           off by default (no verdict read, nothing written), whatever the
           file holds; the key and the tests' switch over them; the
           timestamps before the class (the PowerVR B says why it cannot) */
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_ARM, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF_DEFAULT,
         RHI_SUBPASS_WRITE_NOTHING, RHI_SUBPASS_OFF_LOST_WORDS},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_QCOM, -1, RHI_SUBPASS_PROBING, RHI_SUBPASS_OFF_DEFAULT,
         RHI_SUBPASS_WRITE_NOTHING, RHI_SUBPASS_OFF_LOST_WORDS},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_OTHER, -1, RHI_SUBPASS_KEPT, RHI_SUBPASS_OFF_DEFAULT,
         RHI_SUBPASS_WRITE_NOTHING, RHI_SUBPASS_OFF_OTHER_WORDS},
        {1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_ARM, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_ON,
         RHI_SUBPASS_WRITE_NOTHING, "[dev] subpass_read = on"},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_ARM, 1, RHI_SUBPASS_NONE, RHI_SUBPASS_PROBE,
         RHI_SUBPASS_WRITE_PROBING, "no verdict for this device and driver yet"},
        {-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, 0, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF_DEFAULT,
         RHI_SUBPASS_WRITE_NOTHING, "ICO_VK_SUBPASS_PROBE_CLASS=0"},
        {-1, 1, 0, 1, 0, RHI_SUBPASS_CLASS_OTHER, -1, RHI_SUBPASS_NONE, RHI_SUBPASS_OFF,
         RHI_SUBPASS_WRITE_NOTHING, "no GPU timestamps"},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        char why[256];
        RhiSubpassWrite w = RHI_SUBPASS_WRITE_PROBING;
        const RhiSubpassDecision d =
            rhi_subpass_decide(k[i].ask, k[i].route, k[i].fake, k[i].file, k[i].stamps, k[i].cls,
                               k[i].classEnv, k[i].prev, "why", why, sizeof why, &w);
        if (d != k[i].want || w != k[i].wantWrite ||
            strncmp(why, k[i].wantWhy, strlen(k[i].wantWhy)) != 0) {
            printf("FAIL case %zu: decision %d write %d why \"%s\"\n", i, (int)d, (int)w, why);
            failures++;
        }
    }
    /* no words kept with the verdict */
    char why[256];
    RhiSubpassWrite w;
    CHECK(rhi_subpass_decide(-1, 1, 0, 1, 1, RHI_SUBPASS_CLASS_INTEL, -1, RHI_SUBPASS_KEPT, NULL,
                             why, sizeof why, &w) == RHI_SUBPASS_ON);
    /* this release's defaults */
    CHECK(rhi_subpass_class_default(RHI_SUBPASS_CLASS_INTEL) == 1);
    CHECK(rhi_subpass_class_default(RHI_SUBPASS_CLASS_ARM) == 0);
    CHECK(rhi_subpass_class_default(RHI_SUBPASS_CLASS_QCOM) == 0);
    CHECK(rhi_subpass_class_default(RHI_SUBPASS_CLASS_OTHER) == 0);
    CHECK(strcmp(why, "kept by the probe of an earlier start (no words kept)") == 0);
}

static void test_key_path(void)
{
    char key[32];
    rhi_subpass_verdict_key(key, sizeof key, 0x13B5u, 0x72120000u, 0x80200000u);
    CHECK(strcmp(key, "13b5:72120000:80200000") == 0);
    char p[64];
    CHECK(rhi_subpass_verdict_path(p, sizeof p, "/data/cache/pipelines-ab.vkcache") == 1);
    CHECK(strcmp(p, "/data/cache/" RHI_SUBPASS_VERDICT_NAME) == 0);
    CHECK(rhi_subpass_verdict_path(p, sizeof p, "C:\\Users\\x\\pipelines-ab.vkcache") == 1);
    CHECK(strcmp(p, "C:\\Users\\x\\" RHI_SUBPASS_VERDICT_NAME) == 0);
    CHECK(rhi_subpass_verdict_path(p, sizeof p, "pipelines-ab.vkcache") == 1);
    CHECK(strcmp(p, RHI_SUBPASS_VERDICT_NAME) == 0);
    CHECK(rhi_subpass_verdict_path(p, sizeof p, "") == 0);
    CHECK(rhi_subpass_verdict_path(p, sizeof p, NULL) == 0);
    /* the name does not fit after the folder */
    CHECK(rhi_subpass_verdict_path(p, 24, "/a/long/folder/name/x.vkcache") == 0);
}

static void test_parse(void)
{
    const char *key = "13b5:72120000:80200000";
    char line[RHI_SUBPASS_VERDICT_MAX], why[64];
    CHECK(rhi_subpass_verdict_line(line, sizeof line, key, RHI_SUBPASS_KEPT, "read 1 vs 2 ms") > 0);
    CHECK(strcmp(line, "v1 13b5:72120000:80200000 kept: read 1 vs 2 ms\n") == 0);
    CHECK(rhi_subpass_verdict_parse(line, key, why, sizeof why) == RHI_SUBPASS_KEPT);
    CHECK(strcmp(why, "read 1 vs 2 ms") == 0);
    (void)rhi_subpass_verdict_line(line, sizeof line, key, RHI_SUBPASS_PROBING, NULL);
    CHECK(rhi_subpass_verdict_parse(line, key, why, sizeof why) == RHI_SUBPASS_PROBING);
    CHECK(why[0] == '\0');
    (void)rhi_subpass_verdict_line(line, sizeof line, key, RHI_SUBPASS_REFUSED, "x\r\n");
    CHECK(rhi_subpass_verdict_parse(line, key, why, sizeof why) == RHI_SUBPASS_REFUSED);
    CHECK(strcmp(why, "x") == 0);
    /* another driver, another format, an unknown state, a key's prefix */
    CHECK(rhi_subpass_verdict_parse("v1 13b5:72120000:80200001 kept: x\n", key, why, sizeof why) ==
          RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("v2 13b5:72120000:80200000 kept: x\n", key, why, sizeof why) ==
          RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("v1 13b5:72120000:80200000 maybe: x\n", key, why, sizeof why) ==
          RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("v1 13b5:72120000:80200000kept: x\n", key, why, sizeof why) ==
          RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("v1 13b5:72120000:80200000 keptx: x\n", key, why, sizeof why) ==
          RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("", key, why, sizeof why) == RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse(NULL, key, why, sizeof why) == RHI_SUBPASS_NONE);
    CHECK(rhi_subpass_verdict_parse("v1  kept: x", "", why, sizeof why) == RHI_SUBPASS_NONE);
    /* a why longer than its buffer: cut */
    char tiny[4];
    CHECK(rhi_subpass_verdict_parse("v1 13b5:72120000:80200000 refused: abcdefgh\n", key, tiny,
                                    sizeof tiny) == RHI_SUBPASS_REFUSED);
    CHECK(strcmp(tiny, "abc") == 0);
}

static void test_file(void)
{
    const char *path = "rhi_subpass_verdict_test.txt";
    const char *key = "10005:00000000:00000001";
    char line[RHI_SUBPASS_VERDICT_MAX], text[RHI_SUBPASS_VERDICT_MAX], why[64];
    remove(path);
    CHECK(rhi_subpass_verdict_read(path, text, sizeof text) == 0 && text[0] == '\0');
    (void)rhi_subpass_verdict_line(line, sizeof line, key, RHI_SUBPASS_PROBING, "mark");
    CHECK(rhi_subpass_verdict_write(path, line) == 1);
    CHECK(rhi_subpass_verdict_read(path, text, sizeof text) == 1);
    CHECK(rhi_subpass_verdict_parse(text, key, why, sizeof why) == RHI_SUBPASS_PROBING);
    /* the verdict over the mark: the whole text replaced */
    (void)rhi_subpass_verdict_line(line, sizeof line, key, RHI_SUBPASS_KEPT, "k");
    CHECK(rhi_subpass_verdict_write(path, line) == 1);
    CHECK(rhi_subpass_verdict_read(path, text, sizeof text) == 1 && strcmp(text, line) == 0);
    /* removed */
    CHECK(rhi_subpass_verdict_write(path, NULL) == 1);
    CHECK(rhi_subpass_verdict_read(path, text, sizeof text) == 0);
    CHECK(rhi_subpass_verdict_write(NULL, line) == 0);
    CHECK(rhi_subpass_verdict_write("", line) == 0);
}

int main(void)
{
    test_decide();
    test_key_path();
    test_parse();
    test_file();
    if (failures) {
        printf("rhi_subpass_verdict_test: %d failures\n", failures);
        return 1;
    }
    printf("rhi_subpass_verdict_test: ok\n");
    return 0;
}

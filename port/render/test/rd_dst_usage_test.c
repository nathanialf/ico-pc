/* rd_dst_usage_test.c: the blend policy's footprint (rd_dst_read.c): the
 * fixed-function blend by default makes nothing of the exact blend, and an
 * opt-in makes only the asked mode.
 *
 *   rd_dst_usage_test
 *
 * On a Vulkan device (77 without one; lavapipe has the attachment read),
 * rd_init twice in one process:
 *   1. without ICO_RD_BLEND: the fixed-function blend in force, g_rd.dstModes
 *      only OFF, no destination-read layout or shader twin made, no target
 *      made for a destination read (RdTargetRec.dstUsage without
 *      RD_DST_USAGE_STORAGE or RD_DST_USAGE_ATTACHMENT, SCENE's 0), no pass
 *      begun readable, rd_set_blend_exact refused for both exact modes, and
 *      the start-up set's count N (rd__enumerate_reachable);
 *   2. with ICO_RD_BLEND=attachment (where the device has the attachment
 *      read): the attachment read in force, dstModes OFF and ATTACHMENT,
 *      its layout and twins made (RD_FS_SPRITE's _dstatt), no storage ones,
 *      SCENE made with RD_DST_USAGE_ATTACHMENT; the set is N with the mode
 *      switched to OFF (rd_set_blend_exact) and more with it on;
 *   3. no validation error.
 * The profile's start line is printed by rd_init either way. */
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

static RdPipeKeyInt s_keys[RD_PIPELINE_CACHE_MAX];

static void setBlend(const char *v)
{
#ifdef _WIN32
    _putenv_s("ICO_RD_BLEND", v ? v : "");
#else
    if (v) {
        setenv("ICO_RD_BLEND", v, 1);
    } else {
        unsetenv("ICO_RD_BLEND");
    }
#endif
}

static bool start(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 1.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    return rd_init(640, 448, &st, NULL);
}

static uint32_t setCount(void)
{
    return rd__enumerate_reachable(s_keys, RD_PIPELINE_CACHE_MAX);
}

/* no twin of the mode m (RD_BLEND_EXACT_STORAGE: fsDstRov, else fsDstAtt) */
static bool noTwins(int m)
{
    for (int i = 0; i < RD_FS_COUNT; i++) {
        if ((m == RD_BLEND_EXACT_STORAGE ? g_rd.fsDstRov[i].id : g_rd.fsDstAtt[i].id) != 0) {
            return false;
        }
    }
    return true;
}

static void checkTargets(uint8_t sceneWant)
{
    for (int id = 0; id < RD_TARGET_COUNT; id++) {
        const RdTargetRec *t = rd__target_rec(rd_target((RdTargetId)id).id);
        if (!t) {
            continue;
        }
        CHECK((t->dstUsage & RD_DST_USAGE_STORAGE) == 0, "target %d made for the storage read", id);
        if (sceneWant == 0) {
            CHECK((t->dstUsage & RD_DST_USAGE_ATTACHMENT) == 0,
                  "target %d made for the attachment read", id);
            CHECK(!rd__pass_wants_readable(t), "target %d's passes begun readable", id);
        }
    }
    const RdTargetRec *scene = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    CHECK(scene != NULL, "no SCENE target");
    if (scene) {
        printf("  SCENE dstUsage %u\n", (unsigned)scene->dstUsage);
        CHECK((scene->dstUsage & (RD_DST_USAGE_STORAGE | RD_DST_USAGE_ATTACHMENT)) == sceneWant,
              "SCENE dstUsage %u, %u expected", (unsigned)scene->dstUsage, (unsigned)sceneWant);
    }
}

int main(void)
{
    /* 1. the default */
    setBlend(NULL);
    if (!start()) {
        printf("rd_dst_usage_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    const bool hasAttachment = rhi_limits()->rasterOrderAttachment;
    CHECK(rd_blend_exact() == RD_BLEND_EXACT_OFF, "%s in force by default",
          rd__blend_exact_name(rd_blend_exact()));
    CHECK(g_rd.dstModes == (1u << RD_BLEND_EXACT_OFF), "dstModes %u", (unsigned)g_rd.dstModes);
    CHECK(g_rd.layoutDstStorage.id == 0 && g_rd.layoutDstAttach.id == 0,
          "a destination-read layout made by default");
    CHECK(noTwins(RD_BLEND_EXACT_STORAGE) && noTwins(RD_BLEND_EXACT_ATTACHMENT),
          "a destination-read twin made by default");
    CHECK(g_rd.fsDstAtt[RD_FS_SPRITE].id == 0, "sprite_ps_dstatt made by default");
    checkTargets(0);
    CHECK(!rd_set_blend_exact(RD_BLEND_EXACT_ATTACHMENT), "the attachment read taken by default");
    CHECK(!rd_set_blend_exact(RD_BLEND_EXACT_STORAGE), "the storage read taken by default");
    const uint32_t nFixed = setCount();
    printf("  default: %s, start-up set %u keys\n", rd__blend_exact_name(rd_blend_exact()),
           (unsigned)nFixed);
    CHECK(nFixed > 0, "an empty start-up set");
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();

    /* 2. the opt-in */
    if (!hasAttachment) {
        printf("  the device has no attachment read: the opt-in is not checked\n");
    } else {
        setBlend("attachment");
        if (!start()) {
            CHECK(0, "rd_init with ICO_RD_BLEND=attachment");
        } else {
            CHECK(rd_blend_exact() == RD_BLEND_EXACT_ATTACHMENT, "%s in force with the opt-in",
                  rd__blend_exact_name(rd_blend_exact()));
            CHECK(g_rd.dstModes == ((1u << RD_BLEND_EXACT_OFF) | (1u << RD_BLEND_EXACT_ATTACHMENT)),
                  "dstModes %u", (unsigned)g_rd.dstModes);
            CHECK(g_rd.layoutDstAttach.id != 0 && g_rd.layoutDstStorage.id == 0,
                  "the layouts: attachment %u, storage %u", (unsigned)g_rd.layoutDstAttach.id,
                  (unsigned)g_rd.layoutDstStorage.id);
            CHECK(g_rd.fsDstAtt[RD_FS_SPRITE].id != 0, "no sprite_ps_dstatt with the opt-in");
            CHECK(noTwins(RD_BLEND_EXACT_STORAGE), "a storage twin made for the attachment read");
            checkTargets(RD_DST_USAGE_ATTACHMENT);
            CHECK(!rd_set_blend_exact(RD_BLEND_EXACT_STORAGE), "the storage read taken");
            const uint32_t nWith = setCount();
            CHECK(rd_set_blend_exact(RD_BLEND_EXACT_OFF), "the fixed-function blend refused");
            const uint32_t nOff = setCount();
            CHECK(rd_set_blend_exact(RD_BLEND_EXACT_ATTACHMENT), "the attachment read refused");
            printf("  attachment: start-up set %u keys, %u with the fixed-function blend\n",
                   (unsigned)nWith, (unsigned)nOff);
            CHECK(nOff == nFixed, "the fixed-function set %u, %u by default", (unsigned)nOff,
                  (unsigned)nFixed);
            CHECK(nWith > nFixed, "the attachment read's set %u not above %u", (unsigned)nWith,
                  (unsigned)nFixed);
            CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
                  rhi_vk_validation_error_count());
            rd_shutdown();
        }
        setBlend(NULL);
    }
    if (failures) {
        printf("rd_dst_usage_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_dst_usage_test: ok\n");
    return 0;
}

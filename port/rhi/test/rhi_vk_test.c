/* rhi_vk_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Vulkan backend, headless, then a hazard-tracking cell and the pipeline
 * cache file (rhi_set_pipeline_cache_path: written at shutdown with a version
 * one header, read back at the next init, a file of another device or of
 * garbage ignored and replaced).
 *
 * Exit 0 on success, 1 on a mismatch or a validation error, 77 (skipped)
 * when no Vulkan device can be created.  Set VK_ICD_FILENAMES to pick a
 * driver (lavapipe in the container). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "vk/rhi_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The barriers one render pass adds to a list, read from RhiStats. */
static uint64_t passBarriers(RhiCommandList cl, RhiTexture target)
{
    RhiStats a, b;
    rhi_get_stats(&a);
    RhiRenderPassDesc p = {0};
    p.color[0].texture = target;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = p.height = 16;
    rhi_cmd_begin_render_pass(cl, &p);
    rhi_cmd_end_render_pass(cl);
    rhi_get_stats(&b);
    return b.barriers - a.barriers;
}

/* Package PB's tracked path: a second pass on the same target adds one
 * hazard barrier (write after write), a pass on another target none.
 * ICO_VK_GLOBAL_BARRIERS=1 emits a global barrier before every pass, so the
 * exact counts do not apply and only the run itself is checked. */
static int hazardCell(void)
{
    const char *gb = getenv("ICO_VK_GLOBAL_BARRIERS");
    const bool global = gb && gb[0] && gb[0] != '0';
    if (!rhi_create_backend("vulkan")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test hazards"};
    if (!rhi_init(&dd)) {
        return 0;
    }
    int failures = 0;
    RhiTextureDesc td = {16, 16, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET, "hazardA"};
    RhiTexture a = rhi_create_texture(&td);
    td.debugName = "hazardB";
    RhiTexture b = rhi_create_texture(&td);
    RhiCommandList cl = rhi_begin_commands();
    if (!a.id || !b.id || !cl.id) {
        rhi_test_log("FAIL hazard cell: setup\n");
        failures++;
    } else {
        const RhiTextureBarrier to[2] = {{a, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                         {b, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
        rhi_cmd_barrier(cl, to, 2);
        passBarriers(cl, a);
        const uint64_t again = passBarriers(cl, a);
        const uint64_t other = passBarriers(cl, b);
        rhi_test_log("hazard cell: second pass on A +%llu, pass on B +%llu%s\n",
                     (unsigned long long)again, (unsigned long long)other,
                     global ? " (ICO_VK_GLOBAL_BARRIERS: not asserted)" : "");
        if (!global && (again != 1 || other != 0)) {
            rhi_test_log("FAIL hazard cell: want +1 and +0\n");
            failures++;
        }
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    rhi_wait_idle();
    rhi_destroy_texture(a);
    rhi_destroy_texture(b);
    rhi_shutdown();
    failures += (int)rhi_vk_validation_error_count();
    return failures;
}

/* the cache file's header words (size, version), 0 0 when unreadable */
static void cacheHeader(const char *path, uint32_t h[2])
{
    h[0] = h[1] = 0;
    FILE *fp = fopen(path, "rb");
    if (fp) {
        if (fread(h, 4, 2, fp) != 2) {
            h[0] = h[1] = 0;
        }
        fclose(fp);
    }
}

static int cacheCell(void)
{
    static const char *path = "rhi_vk_test.vkcache";
    int failures = 0;
    remove(path);
    rhi_set_pipeline_cache_path(path);
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 2) {
            /* garbage (another device's file reads the same): ignored */
            FILE *fp = fopen(path, "wb");
            if (fp) {
                static const uint8_t junk[64] = {32, 0, 0, 0, 1, 0, 0, 0, 0xAB};
                fwrite(junk, 1, sizeof(junk), fp);
                fclose(fp);
            }
        }
        RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test cache"};
        if (!rhi_create_backend("vulkan") || !rhi_init(&dd)) {
            rhi_test_log("FAIL cache cell: init %d\n", pass);
            failures++;
            break;
        }
        rhi_shutdown();
        uint32_t h[2];
        cacheHeader(path, h);
        rhi_test_log("cache cell: pass %d: header size %u version %u\n", pass, h[0], h[1]);
        if (h[0] < 32 || h[1] != 1) {
            rhi_test_log("FAIL cache cell: pass %d: no version one header\n", pass);
            failures++;
        }
    }
    rhi_set_pipeline_cache_path(NULL);
    remove(path);
    return failures + (int)rhi_vk_validation_error_count();
}

/* v0.4.2 (Android): under ICO_VK_FAKE_LIMITS=mali (ctest rhi_vk_mali) the
 * device has a Mali-G68's limits, the cells above still draw within them,
 * and what goes past them is refused by the limit's name: a pipeline with
 * five descriptor sets, one with 17 sampled images in the fragment stage,
 * and the memory allocation after maxMemoryAllocationCount (4096, every
 * texture having its own).  Without the variable the cell does nothing. */
static int limitsCell(void)
{
    const char *e = getenv("ICO_VK_FAKE_LIMITS");
    if (!e || strcmp(e, "mali") != 0) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test limits"};
    if (!rhi_create_backend("vulkan") || !rhi_init(&dd)) {
        rhi_test_log("FAIL limits cell: init\n");
        return 1;
    }
    int failures = 0;
#define LIMIT_CHECK(cond, ...)                                                                     \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            rhi_test_log("FAIL limits cell: " __VA_ARGS__);                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
    LIMIT_CHECK(vkr_test_limit("maxBoundDescriptorSets") <= 4 &&
                    vkr_test_limit("maxPerStageDescriptorSampledImages") <= 16 &&
                    vkr_test_limit("maxPushConstantsSize") <= 128 &&
                    vkr_test_limit("maxMemoryAllocationCount") == 4096 &&
                    vkr_test_limit("minStorageBufferOffsetAlignment") >= 256,
                "the limits are not a Mali-G68's\n");
    LIMIT_CHECK(rhi_limits()->uniformAlign >= 256, "uniformAlign %u, want 256 or more\n",
                rhi_limits()->uniformAlign);

    const uint32_t FS = 1u << RHI_STAGE_FRAGMENT;
    const RhiBindSlot ubo = {0, RHI_BIND_UNIFORM_BUFFER, FS};
    RhiBindSlot images[16];
    for (uint32_t i = 0; i < 16; i++) {
        images[i] = (RhiBindSlot){i, RHI_BIND_SAMPLED_TEXTURE, FS};
    }
    const RhiBindGroupLayout one =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){&ubo, 1, "limits ubo"});
    const RhiBindGroupLayout sixteen =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){images, 16, "limits 16 images"});
    const RhiBindGroupLayout single =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){images, 1, "limits 1 image"});
    LIMIT_CHECK(one.id && sixteen.id && single.id, "bind group layouts\n");
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.colorCount = 1;
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.blend[0].writeMask = 0xF;
    const RhiBindGroupLayout five[5] = {one, one, one, one, one};
    d.layouts = five;
    d.layoutCount = 5;
    d.debugName = "limits five sets";
    RhiPipeline p = rhi_create_pipeline(&d);
    const char *hit = vkr_test_last_limit();
    LIMIT_CHECK(!p.id && hit && strcmp(hit, "maxBoundDescriptorSets") == 0,
                "five descriptor sets: %s\n", hit ? hit : "not refused by a limit");
    const RhiBindGroupLayout seventeen[2] = {sixteen, single};
    d.layouts = seventeen;
    d.layoutCount = 2;
    d.debugName = "limits 17 images";
    p = rhi_create_pipeline(&d);
    hit = vkr_test_last_limit();
    LIMIT_CHECK(!p.id && hit && strcmp(hit, "maxPerStageDescriptorSampledImages") == 0,
                "17 sampled images: %s\n", hit ? hit : "not refused by a limit");

    /* textures until the allocation limit refuses one */
    enum { MAX_TRY = 4200 };

    RhiTexture *tex = calloc(MAX_TRY, sizeof(*tex));
    uint32_t made = 0;
    while (tex && made < MAX_TRY) {
        RhiTextureDesc td = {4, 4, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED, "limits"};
        tex[made] = rhi_create_texture(&td);
        if (!tex[made].id) {
            break;
        }
        made++;
    }
    RhiStats st;
    rhi_get_stats(&st);
    hit = vkr_test_last_limit();
    rhi_test_log("limits cell: %u textures made, %llu allocations alive (limit %llu, most %llu, "
                 "%.1f MB)\n",
                 made, (unsigned long long)st.memoryLive, (unsigned long long)st.memoryLimit,
                 (unsigned long long)st.memoryPeak, (double)st.memoryLiveBytes / 1048576.0);
    LIMIT_CHECK(made < MAX_TRY && st.memoryLive == 4096 && st.memoryLimit == 4096 && hit &&
                    strcmp(hit, "maxMemoryAllocationCount") == 0,
                "the allocation limit: %u made, %llu alive, %s\n", made,
                (unsigned long long)st.memoryLive, hit ? hit : "not refused by a limit");
    for (uint32_t i = 0; i < made; i++) {
        rhi_destroy_texture(tex[i]);
    }
    free(tex);
    rhi_destroy_bind_group_layout(one);
    rhi_destroy_bind_group_layout(sixteen);
    rhi_destroy_bind_group_layout(single);
#undef LIMIT_CHECK
    rhi_wait_idle();
    rhi_shutdown();
    return failures + (int)rhi_vk_validation_error_count();
}

int main(void)
{
    const RhiTestConfig cfg = {"vulkan", "rhi_vk_test", true, NULL, rhi_vk_validation_error_count};
    int rc = rhi_test_run_cells(&cfg);
    if (rc != 0) {
        return rc;
    }
    return (hazardCell() || cacheCell() || limitsCell()) ? 1 : 0;
}

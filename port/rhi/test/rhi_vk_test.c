/* rhi_vk_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Vulkan backend, headless, then a hazard-tracking cell and the pipeline
 * cache file (rhi_SetPipelineCachePath: written at shutdown with a version
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
    rhi_GetStats(&a);
    RhiRenderPassDesc p = {0};
    p.color[0].texture = target;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = p.height = 16;
    rhi_CmdBeginRenderPass(cl, &p);
    rhi_CmdEndRenderPass(cl);
    rhi_GetStats(&b);
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
    if (!rhi_CreateBackend("vulkan")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test hazards"};
    if (!rhi_Init(&dd)) {
        return 0;
    }
    int failures = 0;
    RhiTextureDesc td = {16, 16, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET, "hazardA"};
    RhiTexture a = rhi_CreateTexture(&td);
    td.debugName = "hazardB";
    RhiTexture b = rhi_CreateTexture(&td);
    RhiCommandList cl = rhi_BeginCommands();
    if (!a.id || !b.id || !cl.id) {
        rhi_test_Log("FAIL hazard cell: setup\n");
        failures++;
    } else {
        const RhiTextureBarrier to[2] = {{a, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                         {b, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
        rhi_CmdBarrier(cl, to, 2);
        passBarriers(cl, a);
        const uint64_t again = passBarriers(cl, a);
        const uint64_t other = passBarriers(cl, b);
        rhi_test_Log("hazard cell: second pass on A +%llu, pass on B +%llu%s\n",
                     (unsigned long long)again, (unsigned long long)other,
                     global ? " (ICO_VK_GLOBAL_BARRIERS: not asserted)" : "");
        if (!global && (again != 1 || other != 0)) {
            rhi_test_Log("FAIL hazard cell: want +1 and +0\n");
            failures++;
        }
        rhi_EndCommands(cl);
        rhi_Submit(cl);
    }
    rhi_WaitIdle();
    rhi_DestroyTexture(a);
    rhi_DestroyTexture(b);
    rhi_Shutdown();
    failures += (int)rhi_vk_ValidationErrorCount();
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
    rhi_SetPipelineCachePath(path);
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
        if (!rhi_CreateBackend("vulkan") || !rhi_Init(&dd)) {
            rhi_test_Log("FAIL cache cell: init %d\n", pass);
            failures++;
            break;
        }
        rhi_Shutdown();
        uint32_t h[2];
        cacheHeader(path, h);
        rhi_test_Log("cache cell: pass %d: header size %u version %u\n", pass, h[0], h[1]);
        if (h[0] < 32 || h[1] != 1) {
            rhi_test_Log("FAIL cache cell: pass %d: no version one header\n", pass);
            failures++;
        }
    }
    rhi_SetPipelineCachePath(NULL);
    remove(path);
    return failures + (int)rhi_vk_ValidationErrorCount();
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
    if (!rhi_CreateBackend("vulkan") || !rhi_Init(&dd)) {
        rhi_test_Log("FAIL limits cell: init\n");
        return 1;
    }
    int failures = 0;
#define LIMIT_CHECK(cond, ...)                                                                     \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            rhi_test_Log("FAIL limits cell: " __VA_ARGS__);                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
    LIMIT_CHECK(vkr_TestLimit("maxBoundDescriptorSets") <= 4 &&
                    vkr_TestLimit("maxPerStageDescriptorSampledImages") <= 16 &&
                    vkr_TestLimit("maxPushConstantsSize") <= 128 &&
                    vkr_TestLimit("maxMemoryAllocationCount") == 4096 &&
                    vkr_TestLimit("minStorageBufferOffsetAlignment") >= 256,
                "the limits are not a Mali-G68's\n");
    LIMIT_CHECK(rhi_Limits()->uniformAlign >= 256, "uniformAlign %u, want 256 or more\n",
                rhi_Limits()->uniformAlign);

    const uint32_t FS = 1u << RHI_STAGE_FRAGMENT;
    const RhiBindSlot ubo = {0, RHI_BIND_UNIFORM_BUFFER, FS};
    RhiBindSlot images[16];
    for (uint32_t i = 0; i < 16; i++) {
        images[i] = (RhiBindSlot){i, RHI_BIND_SAMPLED_TEXTURE, FS};
    }
    const RhiBindGroupLayout one =
        rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){&ubo, 1, "limits ubo"});
    const RhiBindGroupLayout sixteen =
        rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){images, 16, "limits 16 images"});
    const RhiBindGroupLayout single =
        rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){images, 1, "limits 1 image"});
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
    RhiPipeline p = rhi_CreatePipeline(&d);
    const char *hit = vkr_TestLastLimit();
    LIMIT_CHECK(!p.id && hit && strcmp(hit, "maxBoundDescriptorSets") == 0,
                "five descriptor sets: %s\n", hit ? hit : "not refused by a limit");
    const RhiBindGroupLayout seventeen[2] = {sixteen, single};
    d.layouts = seventeen;
    d.layoutCount = 2;
    d.debugName = "limits 17 images";
    p = rhi_CreatePipeline(&d);
    hit = vkr_TestLastLimit();
    LIMIT_CHECK(!p.id && hit && strcmp(hit, "maxPerStageDescriptorSampledImages") == 0,
                "17 sampled images: %s\n", hit ? hit : "not refused by a limit");

    /* textures until the allocation limit refuses one */
    enum { MAX_TRY = 4200 };

    RhiTexture *tex = calloc(MAX_TRY, sizeof(*tex));
    uint32_t made = 0;
    while (tex && made < MAX_TRY) {
        RhiTextureDesc td = {4, 4, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED, "limits"};
        tex[made] = rhi_CreateTexture(&td);
        if (!tex[made].id) {
            break;
        }
        made++;
    }
    RhiStats st;
    rhi_GetStats(&st);
    hit = vkr_TestLastLimit();
    rhi_test_Log("limits cell: %u textures made, %llu allocations alive (limit %llu, most %llu, "
                 "%.1f MB)\n",
                 made, (unsigned long long)st.memoryLive, (unsigned long long)st.memoryLimit,
                 (unsigned long long)st.memoryPeak, (double)st.memoryLiveBytes / 1048576.0);
    LIMIT_CHECK(made < MAX_TRY && st.memoryLive == 4096 && st.memoryLimit == 4096 && hit &&
                    strcmp(hit, "maxMemoryAllocationCount") == 0,
                "the allocation limit: %u made, %llu alive, %s\n", made,
                (unsigned long long)st.memoryLive, hit ? hit : "not refused by a limit");
    for (uint32_t i = 0; i < made; i++) {
        rhi_DestroyTexture(tex[i]);
    }
    free(tex);
    rhi_DestroyBindGroupLayout(one);
    rhi_DestroyBindGroupLayout(sixteen);
    rhi_DestroyBindGroupLayout(single);
#undef LIMIT_CHECK
    rhi_WaitIdle();
    rhi_Shutdown();
    return failures + (int)rhi_vk_ValidationErrorCount();
}

int main(void)
{
    const RhiTestConfig cfg = {"vulkan", "rhi_vk_test", true, NULL, rhi_vk_ValidationErrorCount};
    int rc = rhi_test_RunCells(&cfg);
    if (rc != 0) {
        return rc;
    }
    return (hazardCell() || cacheCell() || limitsCell()) ? 1 : 0;
}

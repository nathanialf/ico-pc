/* rhi_vk_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Vulkan backend, headless, then a hazard-tracking cell, the render-area
 * check (a pass larger than an attachment is clamped) and the pipeline
 * cache file (rhi_set_pipeline_cache_path: written by rhi_pipeline_cache_save
 * while the device is up and at shutdown, each with a version
 * one header, read back at the next init, a file of another device or of
 * garbage ignored and replaced), the drop of other builds' cache files
 * (vkr_cache_drop_others), and the pipeline pool (poolCell: every
 * pipeline the pool holds, RHI_PIPELINE_POOL_MAX, made and alive at once).
 *
 * Exit 0 on success, 1 on a mismatch or a validation error, 77 (skipped)
 * when no Vulkan device can be created.  Set VK_ICD_FILENAMES to pick a
 * driver (lavapipe in the container). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "rhi_test_spv.h"
#include "vk/rhi_vk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#define MKDIR(d) mkdir((d), 0755)
#endif

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

/* The hazard tracking (vk_cmd.c "Hazards"): a second pass on the same target adds one
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
    RhiTextureDesc td = {16, 16, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET, "hazardA", 0};
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

/* The render-area check (vk_cmd.c vkr_pass_area_fits): a pass larger than
 * its attachments is counted, logged once and clamped (no validation error
 * follows: the area the driver sees fits); a pass that fits is untouched.
 * rhi_cmd_checkpoint and rhi_set_submit_tag are no-ops to the picture
 * (checkpoints off, or not offered: lavapipe has no
 * VK_NV_device_diagnostic_checkpoints). After the wait, rhi_gpu_progress
 * says the submission finished, no wait in progress, under tag 7/3. */
static int passAreaCell(void)
{
    if (!rhi_create_backend("vulkan")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test pass area"};
    if (!rhi_init(&dd)) {
        return 0;
    }
    int failures = 0;
    vkr_test_pass_area_no_assert(true); /* the debug build's assert: the bad passes are wanted */
    RhiTextureDesc td = {16, 16, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET, "areaColour", 0};
    RhiTexture col = rhi_create_texture(&td);
    RhiTextureDesc dd2 = {16, 8, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL, "areaDepth", 0};
    RhiTexture dep = rhi_create_texture(&dd2);
    rhi_set_submit_tag(7, 3);
    RhiCommandList cl = rhi_begin_commands();
    if (!col.id || !dep.id || !cl.id) {
        rhi_test_log("FAIL pass area cell: setup\n");
        failures++;
    } else {
        const RhiTextureBarrier to[2] = {{col, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                         {dep, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE}};
        rhi_cmd_barrier(cl, to, 2);
        const uint32_t n0 = vkr_test_pass_area_count();
        RhiRenderPassDesc p = {0};
        p.color[0].texture = col;
        p.color[0].load = RHI_LOAD_CLEAR;
        p.colorCount = 1;
        p.width = p.height = 16; /* fits */
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_checkpoint(cl, (const void *)(uintptr_t)1);
        rhi_cmd_end_render_pass(cl);
        const uint32_t n1 = vkr_test_pass_area_count();
        p.width = 32; /* wider than the colour */
        p.height = 24;
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
        const uint32_t n2 = vkr_test_pass_area_count();
        p.width = p.height = 16; /* the colour fits, the depth is 8 high */
        p.depth.texture = dep;
        p.depth.depthLoad = RHI_LOAD_CLEAR;
        p.depth.stencilLoad = RHI_LOAD_CLEAR;
        p.depth.clearDepth = 1.0f;
        rhi_cmd_begin_render_pass(cl, &p);
        rhi_cmd_end_render_pass(cl);
        const uint32_t n3 = vkr_test_pass_area_count();
        rhi_test_log("pass area cell: fitting +%u, too wide +%u, depth too short +%u\n", n1 - n0,
                     n2 - n1, n3 - n2);
        if (n1 != n0 || n2 != n1 + 1 || n3 != n2 + 1) {
            rhi_test_log("FAIL pass area cell: want +0, +1, +1\n");
            failures++;
        }
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    rhi_wait_idle();
    {
        /* rhi_gpu_progress after the wait: the submission seen finished,
         * no wait in progress, the tag it was made under */
        RhiGpuProgress gp;
        const bool tracked = rhi_gpu_progress(&gp);
        rhi_test_log("gpu progress: tracked %d, submitted %llu, finished %llu, waiting for %llu, "
                     "replay %u, frame %u\n",
                     (int)tracked, (unsigned long long)gp.submitted,
                     (unsigned long long)gp.finished, (unsigned long long)gp.waitingFor, gp.replay,
                     gp.frame);
        if (!tracked || gp.submitted == 0 || gp.finished != gp.submitted || gp.waitingFor != 0 ||
            gp.replay != 7 || gp.frame != 3) {
            rhi_test_log("FAIL gpu progress: want tracked, finished == submitted > 0, no wait, "
                         "replay 7, frame 3\n");
            failures++;
        }
    }
    vkr_test_pass_area_no_assert(false);
    rhi_destroy_texture(col);
    rhi_destroy_texture(dep);
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
        if (pass == 0) {
            /* written while the device is up (the window's save after the
               start-up set), before any shutdown */
            remove(path);
            uint32_t s[2] = {0, 0};
            const bool saved = rhi_pipeline_cache_save();
            cacheHeader(path, s);
            rhi_test_log("cache cell: saved while up: %d, header size %u version %u\n", (int)saved,
                         s[0], s[1]);
            if (!saved || s[0] < 32 || s[1] != 1) {
                rhi_test_log("FAIL cache cell: rhi_pipeline_cache_save wrote no version one "
                             "header\n");
                failures++;
            }
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

static void touchFile(const char *dir, const char *name)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *fp = fopen(p, "wb");
    if (fp) {
        fputc(1, fp);
        fclose(fp);
    }
}

static int fileExists(const char *dir, const char *name)
{
    char p[256];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    FILE *fp = fopen(p, "rb");
    if (fp) {
        fclose(fp);
    }
    return fp != NULL;
}

/* The other builds' caches: with the file pipelines-<build>.vkcache, every
 * other pipelines*.vkcache and pipelines*.vkcache.tmp in its folder goes
 * (one log name each), the current file, its own .tmp and unrelated files
 * stay; a missing folder and a file of another name drop nothing. */
static int cacheDropCell(void)
{
    static const char *dir = "rhi_vk_test_cachedrop";
    static const char *const old[] = {"pipelines-old.vkcache", "pipelines.vkcache",
                                      "pipelines-old2.vkcache.tmp"};
    static const char *const keep[] = {"pipelines-now.vkcache", "notes.txt", "other.vkcache"};
    char path[256], logbuf[512];
    int failures = 0;
    MKDIR(dir);
    for (size_t i = 0; i < sizeof(old) / sizeof(old[0]); i++) {
        touchFile(dir, old[i]);
    }
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        touchFile(dir, keep[i]);
    }
    snprintf(path, sizeof(path), "%s/pipelines-now.vkcache", dir);
    const uint32_t n = vkr_cache_drop_others(path, logbuf, sizeof(logbuf));
    rhi_test_log("cache drop cell: dropped %u: %s\n", n, logbuf);
    if (n != 3) {
        rhi_test_log("FAIL cache drop cell: dropped %u files, expected 3\n", n);
        failures++;
    }
    for (size_t i = 0; i < sizeof(old) / sizeof(old[0]); i++) {
        if (fileExists(dir, old[i]) || strstr(logbuf, old[i]) == NULL) {
            rhi_test_log("FAIL cache drop cell: %s is still there or was not logged\n", old[i]);
            failures++;
        }
    }
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        if (!fileExists(dir, keep[i])) {
            rhi_test_log("FAIL cache drop cell: %s was removed\n", keep[i]);
            failures++;
        }
    }
    /* a file of another name (the cache cell's) leaves its neighbours */
    snprintf(path, sizeof(path), "%s/custom.vkcache", dir);
    touchFile(dir, "pipelines-old.vkcache");
    if (vkr_cache_drop_others(path, NULL, 0) != 0 || !fileExists(dir, "pipelines-old.vkcache")) {
        rhi_test_log("FAIL cache drop cell: a file not named pipelines*.vkcache dropped one\n");
        failures++;
    }
    /* a folder that does not exist: nothing, no crash */
    if (vkr_cache_drop_others("rhi_vk_test_nonexistent/pipelines-now.vkcache", NULL, 0) != 0) {
        rhi_test_log("FAIL cache drop cell: a missing folder dropped something\n");
        failures++;
    }
    /* rhi_init does it: the old file is gone after the init */
    snprintf(path, sizeof(path), "%s/pipelines-now.vkcache", dir);
    rhi_set_pipeline_cache_path(path);
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test cache drop"};
    if (!rhi_create_backend("vulkan") || !rhi_init(&dd)) {
        rhi_test_log("FAIL cache drop cell: init\n");
        failures++;
    } else {
        if (fileExists(dir, "pipelines-old.vkcache")) {
            rhi_test_log("FAIL cache drop cell: rhi_init left the other build's cache\n");
            failures++;
        }
        rhi_shutdown();
    }
    rhi_set_pipeline_cache_path(NULL);
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
        char p[256];
        snprintf(p, sizeof(p), "%s/%s", dir, keep[i]);
        remove(p);
    }
    snprintf(path, sizeof(path), "%s/pipelines-now.vkcache", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/pipelines-old.vkcache", dir);
    remove(path);
    return failures;
}

/* Android: under ICO_VK_FAKE_LIMITS=mali (ctest rhi_vk_mali) the
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
        RhiTextureDesc td = {4, 4, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED, "limits", 0};
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

static bool envOn(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

/* The pipeline pool (port/rhi/rhi_pipeline_pool.h): RHI_PIPELINE_POOL_MAX
 * pipelines alive at once (the renderer's cache and the headroom for the
 * pipelines made outside it), the first 1100 made one at a time and the
 * rest in batches of 32 (rhi_create_pipelines, as the start-up set), none
 * refused and every id distinct; the 1025th (past the 1024 the pool once
 * held) and the last drawn with, each into its half of a target, read
 * back in its colour (the ids resolve to their pipelines); one more past
 * the pool refused and counted (RhiStats pipelinePoolFull), the most alive
 * at once the pool's size, none alive after they are destroyed.  Run once,
 * not again under the fake devices (rhi_vk_nodual, _d24s8, _mali). */
typedef struct PoolVtx {
    float x, y, z;
    uint8_t rgba[4];
    float u, v;
} PoolVtx;

static int poolCell(void)
{
    if (envOn("ICO_VK_FAKE_NO_DUAL") || envOn("ICO_VK_FAKE_D24S8") || envOn("ICO_VK_FAKE_LIMITS")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test pool"};
    if (!rhi_create_backend("vulkan") || !rhi_init(&dd)) {
        rhi_test_log("FAIL pool cell: init\n");
        return 1;
    }
    int failures = 0;
#define POOL_CHECK(cond, ...)                                                                      \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            rhi_test_log("FAIL pool cell: " __VA_ARGS__);                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
    const RhiShader vs = rhi_create_shader(&(RhiShaderDesc){
        RHI_STAGE_VERTEX, spv_vs_main, sizeof(spv_vs_main), "vs_main", "pool vs_main"});
    const RhiShader ps = rhi_create_shader(&(RhiShaderDesc){
        RHI_STAGE_FRAGMENT, spv_ps_color, sizeof(spv_ps_color), "ps_color", "pool ps_color"});
    static const RhiVertexBinding vb = {0, sizeof(PoolVtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(PoolVtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(PoolVtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(PoolVtx, u)},
    };
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = vs;
    d.fragment = ps;
    d.vertexBindings = &vb;
    d.vertexBindingCount = 1;
    d.vertexAttrs = va;
    d.vertexAttrCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF;
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.colorCount = 1;
    d.debugName = "pool";
    RhiStats a, b;
    rhi_get_stats(&a);
    const uint32_t cap = (uint32_t)a.pipelinePoolSize, live0 = (uint32_t)a.pipelinesLive;
    POOL_CHECK(vs.id && ps.id, "shaders\n");
    POOL_CHECK(cap == RHI_PIPELINE_POOL_MAX && live0 == 0, "pool size %u (want %d), %u alive\n",
               cap, RHI_PIPELINE_POOL_MAX, live0);
    RhiPipeline *p = calloc(RHI_PIPELINE_POOL_MAX, sizeof(*p));
    uint32_t made = 0, refused = 0;
    if (p && vs.id && ps.id && cap == RHI_PIPELINE_POOL_MAX) {
        for (; made < 1100u; made++) {
            p[made] = rhi_create_pipeline(&d);
            refused += p[made].id == 0;
        }
        RhiPipelineDesc descs[32];
        for (uint32_t i = 0; i < 32u; i++) {
            descs[i] = d;
        }
        while (made < cap) {
            const uint32_t nb = cap - made < 32u ? cap - made : 32u;
            rhi_create_pipelines(descs, nb, &p[made], NULL);
            for (uint32_t i = 0; i < nb; i++) {
                refused += p[made + i].id == 0;
            }
            made += nb;
        }
    }
    uint32_t same = 0;
    for (uint32_t i = 0; i < made; i++) {
        for (uint32_t j = i + 1; j < made && p[i].id; j++) {
            same += p[i].id == p[j].id;
        }
    }
    rhi_get_stats(&b);
    rhi_test_log("pool cell: %u pipelines made (%u refused, %u ids shared), %llu alive, most %llu "
                 "of %llu\n",
                 made, refused, same, (unsigned long long)b.pipelinesLive,
                 (unsigned long long)b.pipelinesPeak, (unsigned long long)b.pipelinePoolSize);
    POOL_CHECK(made == RHI_PIPELINE_POOL_MAX && refused == 0 && same == 0,
               "%u made, %u refused, %u ids shared\n", made, refused, same);
    POOL_CHECK(b.pipelinesLive == cap && b.pipelinesPeak == cap &&
                   b.pipelinePoolFull == a.pipelinePoolFull &&
                   b.pipelineFailures == a.pipelineFailures,
               "%llu alive, most %llu, %llu refused for a full pool, %llu failed\n",
               (unsigned long long)b.pipelinesLive, (unsigned long long)b.pipelinesPeak,
               (unsigned long long)(b.pipelinePoolFull - a.pipelinePoolFull),
               (unsigned long long)(b.pipelineFailures - a.pipelineFailures));
    if (made == RHI_PIPELINE_POOL_MAX && refused == 0) {
        /* one more: the pool refuses it and counts it */
        const RhiPipeline over = rhi_create_pipeline(&d);
        rhi_get_stats(&b);
        POOL_CHECK(over.id == 0 && b.pipelinePoolFull == a.pipelinePoolFull + 1u,
                   "one past the pool: id %u, %llu refused for a full pool\n", over.id,
                   (unsigned long long)(b.pipelinePoolFull - a.pipelinePoolFull));
        rhi_destroy_pipeline(over);
        /* the 1025th and the last pipeline draw: red on the left half of a
           4x2 target, green on the right */
        RhiTexture tgt = rhi_create_texture(&(RhiTextureDesc){
            4, 2, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "pool", 0});
        RhiBuffer vbuf = rhi_create_buffer(
            &(RhiBufferDesc){12 * sizeof(PoolVtx), RHI_BUF_VERTEX, RHI_MEM_UPLOAD, "pool verts"});
        PoolVtx *v = vbuf.id ? rhi_map_buffer(vbuf) : NULL;
        POOL_CHECK(tgt.id && v, "target or vertex buffer\n");
        if (tgt.id && v) {
            static const float qx[6] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
            static const float qy[6] = {1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f};
            for (int h = 0; h < 2; h++) {
                for (int i = 0; i < 6; i++) {
                    v[h * 6 + i] = (PoolVtx){qx[i] - 1.0f + (float)h,
                                             qy[i],
                                             0.0f,
                                             {h ? 0 : 255, h ? 255 : 0, 0, 255},
                                             0.0f,
                                             0.0f};
                }
            }
            rhi_wait_frame();
            RhiCommandList cl = rhi_begin_commands();
            RhiTextureBarrier pre = {tgt, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
            rhi_cmd_barrier(cl, &pre, 1);
            RhiRenderPassDesc rp = {0};
            rp.color[0].texture = tgt;
            rp.color[0].load = RHI_LOAD_CLEAR;
            rp.colorCount = 1;
            rp.width = 4;
            rp.height = 2;
            rhi_cmd_begin_render_pass(cl, &rp);
            rhi_cmd_set_vertex_buffer(cl, 0, vbuf, 0);
            rhi_cmd_set_pipeline(cl, p[1024]);
            rhi_cmd_draw(cl, 6, 0, 1);
            rhi_cmd_set_pipeline(cl, p[RHI_PIPELINE_POOL_MAX - 1]);
            rhi_cmd_draw(cl, 6, 6, 1);
            rhi_cmd_end_render_pass(cl);
            RhiTextureBarrier post = {tgt, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
            rhi_cmd_barrier(cl, &post, 1);
            rhi_end_commands(cl);
            rhi_submit(cl);
            uint8_t img[4 * 2 * 4];
            uint32_t pitch = 0;
            const bool read = rhi_readback_texture(tgt, RHI_ASPECT_COLOR, img, sizeof(img), &pitch);
            static const uint8_t red[4] = {255, 0, 0, 255}, green[4] = {0, 255, 0, 255};
            POOL_CHECK(read && pitch == 16 && memcmp(&img[0], red, 4) == 0 &&
                           memcmp(&img[1 * 16 + 3 * 4], green, 4) == 0,
                       "the 1025th and the last pipeline's draws: read %d pitch %u, left %u %u %u "
                       "%u, right %u %u %u %u\n",
                       (int)read, pitch, img[0], img[1], img[2], img[3], img[28], img[29], img[30],
                       img[31]);
        }
        rhi_destroy_buffer(vbuf);
        rhi_destroy_texture(tgt);
    }
    for (uint32_t i = 0; i < made; i++) {
        rhi_destroy_pipeline(p[i]);
    }
    free(p);
    rhi_get_stats(&b);
    POOL_CHECK(b.pipelinesLive == 0, "%llu pipelines alive after they were destroyed\n",
               (unsigned long long)b.pipelinesLive);
    rhi_destroy_shader(vs);
    rhi_destroy_shader(ps);
#undef POOL_CHECK
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
    return (hazardCell() || passAreaCell() || cacheCell() || cacheDropCell() || limitsCell() ||
            poolCell())
               ? 1
               : 0;
}

/* rd_fsr3.c: FSR 3.1 at the upscale point (rd_fsr3.h; docs/port/DISPLAY.md
 * "FSR 3.1 upscaling"): whether it can run, its context, its dispatch.
 *
 * The inputs as the upscale point hands them (rd_upscale.h RdUpscaleIn):
 * SCENE's colour (RGBA8, the whole post chain drawn, jittered), SCENE's
 * depth (D32F_S8: FidelityFX reads its depth aspect; the GS Z grows toward
 * the eye, so the context is made with INVERTED_DEPTH), VELOCITY (RGBA16F,
 * xy texels from the current picture to the one before, +x right, +y down:
 * FSR's own convention, so the motion vector scale is (1, 1) times [dev]
 * velocity_sign), all at the render scale, and UPSCALED (RGBA8, a storage
 * texture) at the display scale.  The states, as rhi_native.h asks: colour
 * and velocity to RHI_STATE_SHADER_READ (Vulkan SHADER_READ_ONLY_OPTIMAL,
 * FSR's COMPUTE_READ), UPSCALED to RHI_STATE_STORAGE (GENERAL, FSR's
 * UNORDERED_ACCESS); the depth's RHI read state is
 * DEPTH_STENCIL_READ_ONLY_OPTIMAL, which FSR's descriptors do not name, so
 * inside the external bracket it is moved to SHADER_READ_ONLY_OPTIMAL (both
 * aspects) for the dispatch and back after it (ffx_link_test runs the same
 * steps on lavapipe with no validation message).  FidelityFX leaves every
 * texture in the state it was given.
 *
 * Per dispatch: the jitter the present took (in->jitter: texels, x right, y
 * down, Halton (2, 3) at index % the phase count this file set from FSR's
 * own query), renderSize the colour's texels, upscaleSize UPSCALED's,
 * frameTimeDelta the present clock's step times the presents since the last
 * dispatch, reset on a new history (a camera cut, the first dispatch, a new
 * size), the frame camera's near and far (swapped, as FSR takes them under
 * INVERTED_DEPTH) and its vertical angle from the game's projection
 * distance, preExposure 1, sharpening at Sharpness / 100 when Sharpness is
 * above 0.  Full pixel: FSR's API has no source or destination offset, so
 * SCENE's texture maps onto UPSCALED's whole (the window and its guard
 * texels together; the window lands within guard x (1 - r/d) texels of
 * its place, under a GS pixel). */
#include "rd_fsr3.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_upscale_temporal.h"
#include "rd_velocity.h"
#include "rhi_native.h"

#ifdef ICO_FFX
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#endif

/* the SDK the build fetches (tools/fetch_deps.sh: v1.1.4 = FSR 3.1.4); the
 * context's provider names its own once one is made */
#define RD_FSR3_SDK_VERSION "3.1.4"

static RdFsr3Stats s_stats;

/* the support answer: -1 not asked since the last rd__fsr3_free */
static int s_supported = -1;
static bool s_testUnsupported;
static char s_why[160];
static char s_toldWhy[160] = "-";

static void rateLog(uint32_t n, const char *what, uint32_t frame)
{
    if (n <= 8u || n % 100u == 0u) {
        rd__log("upscaling: FSR 3.1 not run at frame %u (%s; %u times so far)", frame, what, n);
    }
}

void rd__fsr3_test_unsupported(bool on)
{
    s_testUnsupported = on;
}

const char *rd_upscale_temporal_why(void)
{
    (void)rd_upscale_temporal_supported();
    return s_why;
}

const RdFsr3Stats *rd__fsr3_stats(void)
{
    return &s_stats;
}

bool rd_upscale_temporal_supported(void)
{
    if (s_testUnsupported) {
        snprintf(s_why, sizeof(s_why), "turned off by the test");
        return false;
    }
    if (s_supported >= 0) {
        return s_supported != 0;
    }
    if (!g_rd.hasDevice) {
        /* asked again once there is a device (the support follows it) */
        snprintf(s_why, sizeof(s_why), "no graphics device");
        return false;
    }
#ifndef ICO_FFX
    snprintf(s_why, sizeof(s_why), "this build has no FSR 3.1 (built without the FidelityFX SDK)");
    s_supported = 0;
#else
    RhiNativeDevice nd;
    RhiComputeCaps caps;
    memset(&nd, 0, sizeof(nd));
    memset(&caps, 0, sizeof(caps));
    if (!rhi_native_device(&nd) || nd.kind != RHI_NATIVE_VULKAN) {
        snprintf(s_why, sizeof(s_why), "%s",
                 nd.kind == RHI_NATIVE_D3D12
                     ? "it runs on Vulkan; the Direct3D 12 path is not built"
                     : "it runs on Vulkan, and the graphics API in use is another");
    } else if (nd.apiVersion < VK_API_VERSION_1_2) {
        snprintf(s_why, sizeof(s_why), "the Vulkan driver is %u.%u, FSR 3.1 needs 1.2",
                 VK_API_VERSION_MAJOR(nd.apiVersion), VK_API_VERSION_MINOR(nd.apiVersion));
    } else if (!rhi_device_compute_caps(&caps)) {
        snprintf(s_why, sizeof(s_why), "the device's compute support: %s",
                 caps.why ? caps.why : "not offered");
    } else if (!caps.externalExts) {
        snprintf(s_why, sizeof(s_why),
                 "the device lists VK_KHR_dedicated_allocation without "
                 "VK_KHR_get_memory_requirements2, which FidelityFX would call");
    } else {
        s_why[0] = '\0';
    }
    s_supported = s_why[0] == '\0';
#endif
    if (strcmp(s_why, s_toldWhy) != 0) {
        snprintf(s_toldWhy, sizeof(s_toldWhy), "%s", s_why);
        if (s_why[0]) {
            rd__log("upscaling: FSR 3.1 is not available (%s); a saved \"fsr3\" is drawn as off "
                    "and kept in the config",
                    s_why);
        } else {
            rd__log("upscaling: FSR 3.1 is available (Vulkan, FidelityFX SDK %s)",
                    RD_FSR3_SDK_VERSION);
        }
    }
    return s_supported == 1;
}

bool rd__fsr3_built(void)
{
#ifdef ICO_FFX
    return true;
#else
    return false;
#endif
}

const char *rd__fsr3_not_expected(void)
{
#ifndef ICO_FFX
    return "this build has no FSR 3.1 (built without the FidelityFX SDK)";
#else
    static char why[96];
    RhiNativeDevice nd;
    memset(&nd, 0, sizeof(nd));
    if (!g_rd.hasDevice || !rhi_native_device(&nd)) {
        return "no graphics device";
    }
    if (nd.kind != RHI_NATIVE_VULKAN) {
        return "the graphics API in use is not Vulkan";
    }
    if (nd.apiVersion < VK_API_VERSION_1_2) {
        snprintf(why, sizeof(why), "the Vulkan device is %u.%u, below 1.2",
                 VK_API_VERSION_MAJOR(nd.apiVersion), VK_API_VERSION_MINOR(nd.apiVersion));
        return why;
    }
    return NULL;
#endif
}

#ifdef ICO_FFX

static struct {
    ffxContext ctx;
    uint32_t rw, rh, dw, dh, flags; /* the context's */
    PFN_vkCmdPipelineBarrier barrier;
    bool debugRead, debugView, debugChecks;
} s_f;

static void onMessage(uint32_t type, const wchar_t *message)
{
    const uint32_t n = ++s_stats.messages;
    if (n <= 8u || n % 100u == 0u) {
        rd__log("upscaling: FidelityFX %s: %ls (%u messages so far)",
                type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message ? message : L"?",
                n);
    }
}

static void readDebug(void)
{
    if (s_f.debugRead) {
        return;
    }
    s_f.debugRead = true;
    const char *v = getenv("ICO_RD_UPSCALE_DEBUG_VIEW");
    const char *c = getenv("ICO_RD_UPSCALE_DEBUG_CHECKS");
    s_f.debugView = v && strcmp(v, "1") == 0;
    s_f.debugChecks = c && strcmp(c, "1") == 0;
    if (s_f.debugView || s_f.debugChecks) {
        rd__log("upscaling: FSR 3.1 %s%s%s ([dev] upscale_debug_view / upscale_debug_checks)",
                s_f.debugView ? "draws its debug view" : "",
                s_f.debugView && s_f.debugChecks ? " and " : "",
                s_f.debugChecks ? "checks its inputs (FidelityFX's messages in this log)" : "");
    }
}

/* A context given back keeps its textures until the GPU has finished the
 * pictures that used it: it is retired with the replay it was last used in
 * and destroyed at a dispatch RD_FSR3_RETIRE_REPLAYS replays later (each
 * replay first waits for the frame slot it reuses, rd__wait_frame), or at
 * the renderer's shutdown (the GPU idle) */
#define RD_FSR3_RETIRED 4
#define RD_FSR3_RETIRE_REPLAYS (RHI_FRAMES_IN_FLIGHT + 1u)

static struct {
    ffxContext ctx;
    uint32_t replay;
    char size[48];
} s_retired[RD_FSR3_RETIRED];

static void destroyNow(ffxContext *ctx, const char *size, const char *why)
{
    const ffxReturnCode_t rc = ffxDestroyContext(ctx, NULL);
    *ctx = NULL;
    s_stats.destroyed++;
    rd__log("upscaling: FSR 3.1 context %s destroyed (%s)%s", size, why,
            rc == FFX_API_RETURN_OK ? "" : ", ffxDestroyContext failed");
}

static void retireContext(void)
{
    if (!s_f.ctx) {
        return;
    }
    int slot = -1;
    for (int i = 0; i < RD_FSR3_RETIRED && slot < 0; i++) {
        slot = s_retired[i].ctx ? -1 : i;
    }
    char size[48];
    snprintf(size, sizeof(size), "%ux%u -> %ux%u", s_f.rw, s_f.rh, s_f.dw, s_f.dh);
    if (slot < 0) {
        /* four sizes within a few pictures: the oldest goes now, the GPU
         * idle first */
        if (g_rd.hasDevice) {
            rhi_wait_idle();
        }
        destroyNow(&s_retired[0].ctx, s_retired[0].size, "four retired at once; the GPU idle");
        slot = 0;
    }
    s_retired[slot].ctx = s_f.ctx;
    s_retired[slot].replay = g_rd.replayCounter;
    snprintf(s_retired[slot].size, sizeof(s_retired[slot].size), "%s", size);
    s_f.ctx = NULL;
    s_f.rw = s_f.rh = s_f.dw = s_f.dh = 0;
}

/* at a dispatch (inside a replay, its frame slot waited for) and before
 * every replay (rd__fsr3_collect): a context retired at replay counter C was
 * last recorded in replay C, and replay C + N + 1 (N frames in flight) has
 * waited for the slot replay C + 1 used, which the GPU finished after C */
static void destroyRetired(void)
{
    for (int i = 0; i < RD_FSR3_RETIRED; i++) {
        if (s_retired[i].ctx &&
            g_rd.replayCounter - s_retired[i].replay >= RD_FSR3_RETIRE_REPLAYS) {
            destroyNow(&s_retired[i].ctx, s_retired[i].size, "the pictures that used it finished");
        }
    }
}

static bool ensureContext(const RhiNativeDevice *nd, uint32_t rw, uint32_t rh, uint32_t dw,
                          uint32_t dh, uint32_t frame)
{
    readDebug();
    const uint32_t flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                           (s_f.debugChecks ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0u);
    if (s_f.ctx && s_f.rw == rw && s_f.rh == rh && s_f.dw == dw && s_f.dh == dh &&
        s_f.flags == flags) {
        return true;
    }
    retireContext(); /* a size or a flag changed: destroyed once the GPU is done with it */
    PFN_vkGetDeviceProcAddr gdp = NULL;
    memcpy(&gdp, &nd->getDeviceProcAddr, sizeof(gdp));
    VkDevice device = (VkDevice)nd->device;
    s_f.barrier = gdp ? (PFN_vkCmdPipelineBarrier)gdp(device, "vkCmdPipelineBarrier") : NULL;
    if (!gdp || !s_f.barrier) {
        s_stats.failed++;
        rateLog(s_stats.failed, "no vkGetDeviceProcAddr", frame);
        return false;
    }
    struct ffxCreateBackendVKDesc backend;
    memset(&backend, 0, sizeof(backend));
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkDevice = device;
    backend.vkPhysicalDevice = (VkPhysicalDevice)nd->physicalDevice;
    backend.vkDeviceProcAddr = gdp;
    struct ffxCreateContextDescUpscale up;
    memset(&up, 0, sizeof(up));
    up.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    up.header.pNext = &backend.header;
    up.flags = flags;
    up.maxRenderSize.width = rw;
    up.maxRenderSize.height = rh;
    up.maxUpscaleSize.width = dw;
    up.maxUpscaleSize.height = dh;
    up.fpMessage = onMessage;
    const ffxReturnCode_t rc = ffxCreateContext(&s_f.ctx, &up.header, NULL);
    if (rc != FFX_API_RETURN_OK || !s_f.ctx) {
        s_f.ctx = NULL;
        s_stats.failed++;
        rd__log("upscaling: FSR 3.1 context %ux%u -> %ux%u not made (ffxCreateContext %u); the "
                "reduction reads the scene as it is",
                rw, rh, dw, dh, (unsigned)rc);
        return false;
    }
    s_f.rw = rw;
    s_f.rh = rh;
    s_f.dw = dw;
    s_f.dh = dh;
    s_f.flags = flags;
    s_stats.made++;
    s_stats.renderW = rw;
    s_stats.renderH = rh;
    s_stats.displayW = dw;
    s_stats.displayH = dh;
    /* the provider's version, the GPU memory, the jitter's phase count */
    struct ffxQueryGetProviderVersion pv;
    memset(&pv, 0, sizeof(pv));
    pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (ffxQuery(&s_f.ctx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) {
        snprintf(s_stats.version, sizeof(s_stats.version), "%s", pv.versionName);
    } else {
        snprintf(s_stats.version, sizeof(s_stats.version), "%s", RD_FSR3_SDK_VERSION);
    }
    FfxApiEffectMemoryUsage mem;
    memset(&mem, 0, sizeof(mem));
    struct ffxQueryDescUpscaleGetGPUMemoryUsage mq;
    memset(&mq, 0, sizeof(mq));
    mq.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE;
    mq.gpuMemoryUsageUpscaler = &mem;
    (void)ffxQuery(&s_f.ctx, &mq.header);
    s_stats.memoryMb = (double)mem.totalUsageInBytes / (1024.0 * 1024.0);
    s_stats.aliasableMb = (double)mem.aliasableUsageInBytes / (1024.0 * 1024.0);
    int32_t phases = 0;
    struct ffxQueryDescUpscaleGetJitterPhaseCount pq;
    memset(&pq, 0, sizeof(pq));
    pq.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    pq.renderWidth = rw;
    pq.displayWidth = dw;
    pq.pOutPhaseCount = &phases;
    if (ffxQuery(&s_f.ctx, &pq.header) != FFX_API_RETURN_OK || phases < 1) {
        phases = 1;
    }
    s_stats.phases = phases;
    rd__upscale_set_phases((uint32_t)phases); /* the next presents' jitter (rd_upscale.c) */
    rd__log("upscaling: FSR 3.1 context made at frame %u: render %ux%u -> display %ux%u (ratio "
            "%.2f), jitter phase count %d, FidelityFX %s, GPU memory %.1f MB (%.1f MB "
            "aliasable)%s",
            frame, rw, rh, dw, dh, rh ? (double)dh / (double)rh : 0.0, (int)phases, s_stats.version,
            s_stats.memoryMb, s_stats.aliasableMb, s_f.debugChecks ? ", debug checks on" : "");
    return true;
}

static void fillResource(struct FfxApiResource *r, const RhiNativeTexture *t, uint32_t w,
                         uint32_t h, uint32_t usage, uint32_t state)
{
    memset(r, 0, sizeof(*r));
    r->resource = (void *)(uintptr_t)t->image;
    r->description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r->description.format = ffxApiGetSurfaceFormatVK((VkFormat)t->format);
    r->description.width = w;
    r->description.height = h;
    r->description.depth = 1;
    r->description.mipCount = 1;
    r->description.usage = usage;
    r->state = state;
}

/* the depth between the RHI's read layout and the one FSR's descriptors
 * name, every aspect the format has */
static void depthLayout(VkCommandBuffer cb, const RhiNativeTexture *z, VkImageLayout from,
                        VkImageLayout to)
{
    const VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = (VkImage)(uintptr_t)z->image,
        .subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT |
                (ffxApiIsStencilFormat((VkFormat)z->format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u),
            0, 1, 0, 1}};
    /* compute to compute: rhi_cmd_external_begin's barrier before (every
     * stage's writes to the compute stage) and _end's after chain to it */
    s_f.barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                0, NULL, 0, NULL, 1, &b);
}

bool rd__fsr3_dispatch(RhiCommandList cl, const RdUpscaleIn *in)
{
    const char *why = NULL;
    RhiNativeDevice nd;
    RhiNativeTexture nc, nz, nv, no;
    void *ncb = NULL;
    memset(&nd, 0, sizeof(nd));
    if (!rd_upscale_temporal_supported()) {
        why = "not available";
    } else if (!in->color.id || !in->out.id) {
        why = "no SCENE or UPSCALED";
    } else if (!in->depth.id) {
        why = "no depth";
    } else if (!in->velocity.id || !in->velocityState) {
        why = "no motion vectors";
    } else if (!rhi_native_device(&nd) || nd.kind != RHI_NATIVE_VULKAN ||
               !rhi_native_texture(in->color, &nc) || !rhi_native_texture(in->depth, &nz) ||
               !rhi_native_texture(in->velocity, &nv) || !rhi_native_texture(in->out, &no) ||
               !rhi_native_command_list(cl, &ncb)) {
        why = "no native handles";
    } else if (!no.storage) {
        why = "UPSCALED is not a storage texture";
    } else if (nc.nativeState[RHI_STATE_SHADER_READ] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
               nv.nativeState[RHI_STATE_SHADER_READ] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
               no.nativeState[RHI_STATE_STORAGE] != VK_IMAGE_LAYOUT_GENERAL ||
               nz.nativeState[RHI_STATE_DEPTH_READ] != nz.nativeState[RHI_STATE_SHADER_READ]) {
        why = "a texture's layout is not the one FidelityFX is told";
    }
    if (why) {
        rateLog(++s_stats.refused, why, in->frame);
        return false;
    }
    destroyRetired();
    if (!ensureContext(&nd, in->colorW, in->colorH, in->outW, in->outH, in->frame)) {
        return false;
    }
    /* a replay whose velocity pass did not run (no VU work drawn after
     * list 0's): VELOCITY still holds an older picture's motion, or nothing
     * at all; cleared to zero motion here */
    if (rd__velocity_pass_replay() != g_rd.replayCounter) {
        RhiRenderPassDesc rp;
        memset(&rp, 0, sizeof(rp));
        rp.color[0].texture = in->velocity;
        rp.color[0].load = RHI_LOAD_CLEAR;
        rp.colorCount = 1;
        rp.width = in->velocityW;
        rp.height = in->velocityH;
        rd__transition(cl, in->velocity, in->velocityState, RHI_STATE_RENDER_TARGET);
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_end_render_pass(cl);
        s_stats.velocityCleared++;
    }
    /* the states rhi_native.h asks; the depth is read where it is (in
     * place DEPTH_READ, the copy SHADER_READ: one Vulkan layout) */
    rd__transition(cl, in->color, in->colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, in->velocity, in->velocityState, RHI_STATE_SHADER_READ);
    rd__transition(cl, in->out, in->outState, RHI_STATE_STORAGE);
    if (!rhi_cmd_external_begin(cl)) {
        rateLog(++s_stats.refused, "the external bracket was refused", in->frame);
        return false;
    }
    VkCommandBuffer cb = (VkCommandBuffer)ncb;
    const VkImageLayout zRead = (VkImageLayout)nz.nativeState[RHI_STATE_DEPTH_READ];
    depthLayout(cb, &nz, zRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    struct ffxDispatchDescUpscale d;
    memset(&d, 0, sizeof(d));
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = ncb;
    fillResource(&d.color, &nc, in->colorW, in->colorH, FFX_API_RESOURCE_USAGE_READ_ONLY,
                 FFX_API_RESOURCE_STATE_COMPUTE_READ);
    fillResource(
        &d.depth, &nz, in->depthW ? in->depthW : in->colorW, in->depthH ? in->depthH : in->colorH,
        FFX_API_RESOURCE_USAGE_DEPTHTARGET | (ffxApiIsStencilFormat((VkFormat)nz.format)
                                                  ? (uint32_t)FFX_API_RESOURCE_USAGE_STENCILTARGET
                                                  : 0u),
        FFX_API_RESOURCE_STATE_COMPUTE_READ);
    fillResource(&d.motionVectors, &nv, in->velocityW, in->velocityH,
                 FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    fillResource(&d.output, &no, in->outW, in->outH, FFX_API_RESOURCE_USAGE_UAV,
                 FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset.x = in->jitter[0];
    d.jitterOffset.y = in->jitter[1];
    const float sign = rd__velocity_sign();
    d.motionVectorScale.x = sign;
    d.motionVectorScale.y = sign;
    d.renderSize.width = in->colorW;
    d.renderSize.height = in->colorH;
    d.upscaleSize.width = in->outW;
    d.upscaleSize.height = in->outH;
    const uint8_t sharp = g_rd.settings.upscaleSharpness;
    d.enableSharpening = sharp > 0u;
    d.sharpness = (float)(sharp > 100u ? 100u : sharp) / 100.0f;
    double step = rd__present_clock_step_ms();
    if (!(step > 0.0)) {
        step = 1000.0 / 60.0; /* untimed (the tools): a 60 Hz present */
    }
    const double dt = step * (double)(in->presents ? in->presents : 1u);
    d.frameTimeDelta = (float)(dt < 1.0 ? 1.0 : dt);
    d.preExposure = 1.0f;
    d.reset = in->reset;
    /* the GS Z grows toward the eye (INVERTED_DEPTH): FSR takes the far
     * plane as cameraNear then */
    d.cameraNear = in->camera ? in->cameraFar : 10000.0f;
    d.cameraFar = in->camera ? in->cameraNear : 1.0f;
    d.cameraFovAngleVertical = in->camera ? in->fovY : 1.0f;
    d.viewSpaceToMetersFactor = 1.0f;
    d.flags = s_f.debugView ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0u;
    const ffxReturnCode_t rc = ffxDispatch(&s_f.ctx, &d.header);

    depthLayout(cb, &nz, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, zRead);
    rhi_cmd_external_end(cl);
    if (rc != FFX_API_RETURN_OK) {
        s_stats.failed++;
        char what[48];
        snprintf(what, sizeof(what), "ffxDispatch returned %u", (unsigned)rc);
        rateLog(s_stats.failed, what, in->frame);
        return false;
    }
    s_stats.dispatched++;
    s_stats.resets += in->reset ? 1u : 0u;
    return true;
}

void rd__fsr3_collect(void)
{
    destroyRetired();
}

void rd__fsr3_free(void)
{
    if (s_f.ctx) {
        rd__upscale_set_phases(0); /* the next context's count, at its first dispatch */
    }
    retireContext();
    s_supported = -1;
}

void rd__fsr3_shutdown(void)
{
    retireContext();
    bool any = false;
    for (int i = 0; i < RD_FSR3_RETIRED; i++) {
        any = any || s_retired[i].ctx;
    }
    if (any && g_rd.hasDevice) {
        rhi_wait_idle();
    }
    for (int i = 0; i < RD_FSR3_RETIRED; i++) {
        if (s_retired[i].ctx) {
            destroyNow(&s_retired[i].ctx, s_retired[i].size, "the renderer stopped");
        }
    }
    s_supported = -1;
}

#else /* ICO_FFX */

bool rd__fsr3_dispatch(RhiCommandList cl, const RdUpscaleIn *in)
{
    (void)cl;
    rateLog(++s_stats.refused, "this build has no FSR 3.1", in ? in->frame : 0u);
    return false;
}

void rd__fsr3_collect(void) {}

void rd__fsr3_free(void)
{
    s_supported = -1;
}

void rd__fsr3_shutdown(void)
{
    s_supported = -1;
}

#endif /* ICO_FFX */

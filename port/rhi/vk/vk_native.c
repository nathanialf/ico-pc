/* vk_native.c: rhi_native.h on Vulkan, the handles external compute (the
 * FidelityFX Vulkan backend) is created from.  rhi_cmd_external_begin and
 * _end, which touch the command list's cached state, are in vk_cmd.c; the
 * capability answers are vk_device.c's vkr_probe_compute. */
#include "vk_internal.h"
#include <string.h>

#define VKR_H(x) ((uint64_t)(x))

bool rhi_native_device(RhiNativeDevice *out)
{
    if (!out || !g_vkr.initialised || !g_vkr.device) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RHI_NATIVE_VULKAN;
    out->instance = (void *)g_vkr.instance;
    out->device = (void *)g_vkr.device;
    out->physicalDevice = (void *)g_vkr.phys;
    out->queue = (void *)g_vkr.queue;
    out->queueFamily = g_vkr.queueFamily;
    out->apiVersion = g_vkr.apiVersion;
    /* function pointers into data pointers by copy, not by cast */
    PFN_vkGetInstanceProcAddr gip = vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdp = vkGetDeviceProcAddr;
    memcpy(&out->getInstanceProcAddr, &gip, sizeof(gip));
    memcpy(&out->getDeviceProcAddr, &gdp, sizeof(gdp));
    return true;
}

bool rhi_native_texture(RhiTexture t, RhiNativeTexture *out)
{
    VkrTexture *tex = vkr_get_texture(t);
    if (!out || !tex || !tex->image) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RHI_NATIVE_VULKAN;
    out->image = VKR_H(tex->image);
    out->format = (uint32_t)tex->format;
    out->width = tex->width;
    out->height = tex->height;
    out->mips = tex->mips;
    out->samples = tex->samples;
    out->storage = tex->storage;
    out->stateTracked = false;
    for (uint32_t s = 0; s < RHI_STATE_COUNT; s++) {
        out->nativeState[s] = (uint32_t)vkr_state_layout((RhiState)s, tex->rhiFormat);
    }
    return true;
}

bool rhi_native_command_list(RhiCommandList cl, void **out)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!out || !c || !c->recording) {
        return false;
    }
    *out = (void *)c->cb;
    return true;
}

bool rhi_device_compute_caps(RhiComputeCaps *out)
{
    if (!out) {
        return false;
    }
    *out = g_vkr.compute;
    return out->ok;
}

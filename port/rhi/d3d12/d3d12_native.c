/* d3d12_native.c: rhi_native.h on D3D12, the handles external compute (the
 * FidelityFX DX12 backend) is created from.  rhi_cmd_external_begin and _end
 * are in d3d12_cmd.c; the capability answers are d3d12_device.c's
 * dx_probe_compute (decided by d3dp_compute_caps, d3d12_plan.c). */
#include "d3d12_internal.h"
#include <string.h>

bool rhi_native_device(RhiNativeDevice *out)
{
    if (!out || !g_dx.initialised || !g_dx.device) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->kind = RHI_NATIVE_D3D12;
    out->device = (void *)g_dx.device;
    out->queue = (void *)g_dx.queue;
    return true;
}

bool rhi_native_texture(RhiTexture t, RhiNativeTexture *out)
{
    DxTexture *tex = dx_get_texture(t);
    if (!out || !tex || !tex->res) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const bool depth = tex->rhiFormat == RHI_FMT_D32F || tex->rhiFormat == RHI_FMT_D32F_S8;
    out->kind = RHI_NATIVE_D3D12;
    out->image = (uint64_t)(uintptr_t)tex->res;
    out->format = (uint32_t)tex->resFormat;
    out->width = tex->width;
    out->height = tex->height;
    out->mips = tex->mips;
    out->samples = tex->samples;
    out->storage = tex->uav;
    out->stateTracked = true;
    out->state = tex->state;
    for (uint32_t s = 0; s < RHI_STATE_COUNT; s++) {
        out->nativeState[s] = d3dp_texture_state((RhiState)s, depth);
    }
    return true;
}

bool rhi_native_command_list(RhiCommandList cl, void **out)
{
    DxCmdList *c = dx_get_cmd(cl);
    if (!out || !c || !c->recording) {
        return false;
    }
    *out = (void *)c->cl;
    return true;
}

bool rhi_device_compute_caps(RhiComputeCaps *out)
{
    if (!out) {
        return false;
    }
    *out = g_dx.compute;
    return out->ok;
}

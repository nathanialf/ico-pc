// ico_ffx_provider.cpp: ffx-api's provider table for the port's build of
// the FidelityFX SDK (port/rhi/ffx/CMakeLists.txt), in place of the SDK's
// ffx-api/src/ffx_provider.cpp.
//
// The SDK's table lists the FSR 3.1 upscaler, FSR 2, frame generation and
// the frame-generation swapchains, and asks the AMD D3D12 driver
// (amdxc64.dll) for newer providers of its own. The port builds the FSR 3.1
// upscaler alone, so the table here holds that one provider and nothing is
// probed: the four functions ffx_api.cpp calls (ffx_provider.h) over a
// one-entry table, with the SDK's semantics (an override id picks the
// provider with that id; otherwise the first that can provide the
// description).
#include "ffx_provider.h"
#include "ffx_provider_fsr3upscale.h"

#include <cstdint>

static const ffxProvider* const s_providers[] = {
    &ffxProvider_FSR3Upscale::Instance,
};
static constexpr size_t s_providerCount = sizeof(s_providers) / sizeof(s_providers[0]);

const ffxProvider* GetffxProvider(ffxStructType_t descType, uint64_t overrideId, void* device)
{
    (void)device;
    for (size_t i = 0; i < s_providerCount; ++i)
    {
        if (overrideId && s_providers[i]->GetId() != overrideId)
            continue;
        if (s_providers[i]->CanProvide(descType))
            return s_providers[i];
    }
    return nullptr;
}

const ffxProvider* GetAssociatedProvider(ffxContext* context)
{
    const InternalContextHeader* hdr = (const InternalContextHeader*)(*context);
    return hdr->provider;
}

uint64_t GetProviderVersions(ffxStructType_t descType, void* device, uint64_t capacity, uint64_t* versionIds,
                             const char** versionNames)
{
    (void)device;
    uint64_t count = 0;
    for (size_t i = 0; i < s_providerCount && count < capacity; ++i)
    {
        if (!s_providers[i]->CanProvide(descType))
            continue;
        if (versionIds)
            versionIds[count] = s_providers[i]->GetId();
        if (versionNames)
            versionNames[count] = s_providers[i]->GetVersionName();
        count++;
    }
    return count;
}

uint64_t GetProviderCount(ffxStructType_t descType, void* device)
{
    return GetProviderVersions(descType, device, UINT64_MAX, nullptr, nullptr);
}

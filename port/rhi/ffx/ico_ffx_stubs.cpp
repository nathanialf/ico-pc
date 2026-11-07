// ico_ffx_stubs.cpp: what the FidelityFX Vulkan backend (ffx_vk.cpp) names
// from the parts of the SDK the port does not build
// (port/rhi/ffx/CMakeLists.txt).
//
// The backend's interface table points its frame-generation swapchain
// entry at ffxSetFrameGenerationConfigToSwapchainVK, which lives in the
// SDK's FrameInterpolationSwapchain sources. Frame generation is not built
// and nothing in the port calls that entry, so it answers as the SDK's own
// does when there is no frame-interpolation swapchain to configure.
#include <FidelityFX/host/backends/vk/ffx_vk.h>

FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const* config)
{
    (void)config;
    return FFX_ERROR_INVALID_ARGUMENT;
}

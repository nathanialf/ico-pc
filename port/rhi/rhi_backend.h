/* rhi_backend.h: how several backends of port/rhi/rhi.h share one build.
 *
 * Each backend library is compiled with RHI_BACKEND_PREFIX set to its short
 * name (vk, d3d12).  rhi.h then includes rhi_backend_names.h first, whose
 * macros rename every rhi_* entry point the backend defines to
 * rhi_<prefix>_*, so the backend's sources keep the plain rhi.h names.  The
 * backend registers its functions once with RHI_BACKEND_DEFINE (one line,
 * port/rhi/vk/vk_device.c and port/rhi/d3d12/d3d12_device.c), and
 * port/rhi/rhi_backend.c defines the real rhi_* functions as forwarders to
 * the table rhi_CreateBackend selected.
 *
 * Callers never include this header; it is for backends and the dispatcher.
 */
#ifndef PORT_RHI_RHI_BACKEND_H
#define PORT_RHI_RHI_BACKEND_H

#include "rhi.h"

/* X(name): every entry point of rhi.h a backend implements, in the order of
 * RhiBackendTable. */
#define RHI_BACKEND_FUNCS(X)                                                                       \
    X(Init)                                                                                        \
    X(Shutdown)                                                                                    \
    X(Backend)                                                                                     \
    X(Limits)                                                                                      \
    X(AdapterName)                                                                                 \
    X(DeviceLost)                                                                                  \
    X(ResizeSwapchain)                                                                             \
    X(SwapchainFormat)                                                                             \
    X(SwapchainSize)                                                                               \
    X(AcquireBackbuffer)                                                                           \
    X(Present)                                                                                     \
    X(ReleaseSurface)                                                                              \
    X(RecreateSurface)                                                                             \
    X(CreateBuffer)                                                                                \
    X(DestroyBuffer)                                                                               \
    X(MapBuffer)                                                                                   \
    X(UnmapBuffer)                                                                                 \
    X(CreateTexture)                                                                               \
    X(DestroyTexture)                                                                              \
    X(CreateSampler)                                                                               \
    X(DestroySampler)                                                                              \
    X(CreateShader)                                                                                \
    X(DestroyShader)                                                                               \
    X(CreateBindGroupLayout)                                                                       \
    X(DestroyBindGroupLayout)                                                                      \
    X(CreateBindGroup)                                                                             \
    X(CreatePipeline)                                                                              \
    X(DestroyPipeline)                                                                             \
    X(BeginCommands)                                                                               \
    X(EndCommands)                                                                                 \
    X(Submit)                                                                                      \
    X(WaitFrame)                                                                                   \
    X(FrameSlot)                                                                                   \
    X(WaitIdle)                                                                                    \
    X(CmdBarrier)                                                                                  \
    X(CmdBeginRenderPass)                                                                          \
    X(CmdEndRenderPass)                                                                            \
    X(CmdSetViewport)                                                                              \
    X(CmdSetScissor)                                                                               \
    X(CmdSetPipeline)                                                                              \
    X(CmdSetBindGroup)                                                                             \
    X(CmdSetBindGroupOffsets)                                                                      \
    X(CmdSetVertexBuffer)                                                                          \
    X(CmdSetIndexBuffer)                                                                           \
    X(CmdSetStencilRef)                                                                            \
    X(CmdSetBlendConstant)                                                                         \
    X(CmdDraw)                                                                                     \
    X(CmdDrawIndexed)                                                                              \
    X(CmdCopyBuffer)                                                                               \
    X(CmdCopyBufferToTexture)                                                                      \
    X(CmdCopyTexture)                                                                              \
    X(CmdCopyTextureToBuffer)                                                                      \
    X(CmdBeginLabel)                                                                               \
    X(CmdEndLabel)                                                                                 \
    X(ReadbackTexture)                                                                             \
    X(GetStats)                                                                                    \
    X(TimestampsSupported)                                                                         \
    X(CmdWriteTimestamp)                                                                           \
    X(ReadTimestamps)                                                                              \
    X(PreferMailbox)                                                                               \
    X(PresentMailbox)                                                                              \
    X(PresentModeName)                                                                             \
    X(SetPipelineCachePath)                                                                        \
    X(InjectorName)                                                                                \
    X(OverlayName)

/* One function pointer per entry point, typed from rhi.h's declaration (in
 * a backend's sources rhi_##n pastes to the renamed function, which has the
 * same type). */
#define RHI__TABLE_FIELD(n) __typeof__(rhi_##n) *n;

typedef struct RhiBackendTable {
    const char *name; /* "vulkan", "d3d12": the rhi_CreateBackend name */
    RHI_BACKEND_FUNCS(RHI__TABLE_FIELD)
} RhiBackendTable;

/* In a backend source compiled with RHI_BACKEND_PREFIX: defines the table
 * `sym` with every entry point of this backend. */
#define RHI__TABLE_INIT(n) .n = rhi_##n,
#define RHI_BACKEND_DEFINE(sym, nameString)                                                        \
    const RhiBackendTable sym = {.name = (nameString), RHI_BACKEND_FUNCS(RHI__TABLE_INIT)}

extern const RhiBackendTable rhi_backend_vk;
extern const RhiBackendTable rhi_backend_d3d12;

#endif /* PORT_RHI_RHI_BACKEND_H */

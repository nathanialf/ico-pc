/* rhi_backend_names.h: renames rhi.h's entry points for a backend library.
 *
 * rhi.h includes this file when RHI_BACKEND_PREFIX is defined (each backend
 * library is compiled with it: vk, d3d12), so a backend's definition of
 * rhi_Init becomes rhi_<prefix>_Init and so on; port/rhi/rhi_backend.h
 * describes the scheme.  Every entry point of rhi.h must be listed here and
 * in RHI_BACKEND_FUNCS (rhi_backend.h).
 */
#ifndef PORT_RHI_RHI_BACKEND_NAMES_H
#define PORT_RHI_RHI_BACKEND_NAMES_H

#ifdef RHI_BACKEND_PREFIX

#define RHI__CAT3(a, b, c) a##b##c
#define RHI__XCAT3(a, b, c) RHI__CAT3(a, b, c)
#define RHI__NAME(fn) RHI__XCAT3(rhi_, RHI_BACKEND_PREFIX, _##fn)

/* clang-format off */
#define rhi_Init                RHI__NAME(Init)
#define rhi_Shutdown            RHI__NAME(Shutdown)
#define rhi_Backend             RHI__NAME(Backend)
#define rhi_Limits              RHI__NAME(Limits)
#define rhi_AdapterName         RHI__NAME(AdapterName)
#define rhi_DeviceLost          RHI__NAME(DeviceLost)
#define rhi_ResizeSwapchain     RHI__NAME(ResizeSwapchain)
#define rhi_SwapchainFormat     RHI__NAME(SwapchainFormat)
#define rhi_AcquireBackbuffer   RHI__NAME(AcquireBackbuffer)
#define rhi_Present             RHI__NAME(Present)
#define rhi_CreateBuffer        RHI__NAME(CreateBuffer)
#define rhi_DestroyBuffer       RHI__NAME(DestroyBuffer)
#define rhi_MapBuffer           RHI__NAME(MapBuffer)
#define rhi_UnmapBuffer         RHI__NAME(UnmapBuffer)
#define rhi_CreateTexture       RHI__NAME(CreateTexture)
#define rhi_DestroyTexture      RHI__NAME(DestroyTexture)
#define rhi_CreateSampler       RHI__NAME(CreateSampler)
#define rhi_DestroySampler      RHI__NAME(DestroySampler)
#define rhi_CreateShader        RHI__NAME(CreateShader)
#define rhi_DestroyShader       RHI__NAME(DestroyShader)
#define rhi_CreateBindGroupLayout  RHI__NAME(CreateBindGroupLayout)
#define rhi_DestroyBindGroupLayout RHI__NAME(DestroyBindGroupLayout)
#define rhi_CreateBindGroup     RHI__NAME(CreateBindGroup)
#define rhi_CreatePipeline      RHI__NAME(CreatePipeline)
#define rhi_DestroyPipeline     RHI__NAME(DestroyPipeline)
#define rhi_BeginCommands       RHI__NAME(BeginCommands)
#define rhi_EndCommands         RHI__NAME(EndCommands)
#define rhi_Submit              RHI__NAME(Submit)
#define rhi_WaitFrame           RHI__NAME(WaitFrame)
#define rhi_WaitIdle            RHI__NAME(WaitIdle)
#define rhi_CmdBarrier          RHI__NAME(CmdBarrier)
#define rhi_CmdBeginRenderPass  RHI__NAME(CmdBeginRenderPass)
#define rhi_CmdEndRenderPass    RHI__NAME(CmdEndRenderPass)
#define rhi_CmdSetViewport      RHI__NAME(CmdSetViewport)
#define rhi_CmdSetScissor       RHI__NAME(CmdSetScissor)
#define rhi_CmdSetPipeline      RHI__NAME(CmdSetPipeline)
#define rhi_CmdSetBindGroup     RHI__NAME(CmdSetBindGroup)
#define rhi_CmdSetBindGroupOffsets RHI__NAME(CmdSetBindGroupOffsets)
#define rhi_CmdSetVertexBuffer  RHI__NAME(CmdSetVertexBuffer)
#define rhi_CmdSetIndexBuffer   RHI__NAME(CmdSetIndexBuffer)
#define rhi_CmdSetStencilRef    RHI__NAME(CmdSetStencilRef)
#define rhi_CmdSetBlendConstant RHI__NAME(CmdSetBlendConstant)
#define rhi_CmdDraw             RHI__NAME(CmdDraw)
#define rhi_CmdDrawIndexed      RHI__NAME(CmdDrawIndexed)
#define rhi_CmdCopyBuffer       RHI__NAME(CmdCopyBuffer)
#define rhi_CmdCopyBufferToTexture RHI__NAME(CmdCopyBufferToTexture)
#define rhi_CmdCopyTexture      RHI__NAME(CmdCopyTexture)
#define rhi_CmdCopyTextureToBuffer RHI__NAME(CmdCopyTextureToBuffer)
#define rhi_CmdBeginLabel       RHI__NAME(CmdBeginLabel)
#define rhi_CmdEndLabel         RHI__NAME(CmdEndLabel)
#define rhi_ReadbackTexture     RHI__NAME(ReadbackTexture)
#define rhi_GetStats            RHI__NAME(GetStats)
#define rhi_TimestampsSupported RHI__NAME(TimestampsSupported)
#define rhi_CmdWriteTimestamp   RHI__NAME(CmdWriteTimestamp)
#define rhi_ReadTimestamps      RHI__NAME(ReadTimestamps)
#define rhi_PreferMailbox       RHI__NAME(PreferMailbox)
#define rhi_PresentMailbox      RHI__NAME(PresentMailbox)
/* clang-format on */

#endif /* RHI_BACKEND_PREFIX */

#endif /* PORT_RHI_RHI_BACKEND_NAMES_H */

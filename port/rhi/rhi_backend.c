/* rhi_backend.c: the rhi.h entry points, forwarded to the backend
 * rhi_CreateBackend selected (rhi_backend.h describes the scheme).
 *
 * The backends linked into the build are listed by ICO_RHI_HAVE_VK and
 * ICO_RHI_HAVE_D3D12 (port/rhi/CMakeLists.txt), Vulkan first: it stays the
 * default until the Windows presets switch. */
#include "rhi_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const RhiBackendTable *const s_backends[] = {
#ifdef ICO_RHI_HAVE_VK
    &rhi_backend_vk,
#endif
#ifdef ICO_RHI_HAVE_D3D12
    &rhi_backend_d3d12,
#endif
    NULL};

static const RhiBackendTable *s_sel;
static bool s_up; /* rhi_Init succeeded and rhi_Shutdown has not run */

static bool nameEq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') {
            x = (char)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char)(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

static const RhiBackendTable *find(const char *name)
{
    /* "vk" is accepted for "vulkan" */
    if (name && nameEq(name, "vk")) {
        name = "vulkan";
    }
    for (const RhiBackendTable *const *p = s_backends; *p; p++) {
        if (name && nameEq(name, (*p)->name)) {
            return *p;
        }
    }
    return NULL;
}

static const RhiBackendTable *defaultBackend(void)
{
    const char *env = getenv("ICO_RHI_BACKEND");
    const RhiBackendTable *t = (env && *env) ? find(env) : NULL;
    if (env && *env && !t) {
        fprintf(stderr, "rhi: ICO_RHI_BACKEND=%s is not linked into this build; using %s\n", env,
                s_backends[0] ? s_backends[0]->name : "nothing");
    }
    return t ? t : s_backends[0];
}

bool rhi_CreateBackend(const char *name)
{
    if (s_up) {
        fprintf(stderr, "rhi: rhi_CreateBackend(%s) while a device is up\n", name ? name : "");
        return false;
    }
    const RhiBackendTable *t = (name && *name) ? find(name) : defaultBackend();
    if (!t) {
        fprintf(stderr, "rhi: backend \"%s\" is not linked into this build\n",
                name ? name : "(default)");
        return false;
    }
    s_sel = t;
    return true;
}

const char *rhi_BackendName(uint32_t index)
{
    for (uint32_t i = 0; s_backends[i]; i++) {
        if (i == index) {
            return s_backends[i]->name;
        }
    }
    return NULL;
}

static const RhiBackendTable *be(void)
{
    if (!s_sel) {
        s_sel = defaultBackend();
        if (!s_sel) {
            fprintf(stderr, "rhi: no backend linked into this build\n");
            abort();
        }
    }
    return s_sel;
}

/* ------------------------------------------------------------- forwarders */
bool rhi_Init(const RhiDeviceDesc *desc)
{
    if (s_up) {
        return true;
    }
    s_up = be()->Init(desc);
    return s_up;
}

void rhi_Shutdown(void)
{
    be()->Shutdown();
    s_up = false;
}

RhiBackendKind rhi_Backend(void)
{
    return be()->Backend();
}

const RhiLimits *rhi_Limits(void)
{
    return be()->Limits();
}

const char *rhi_AdapterName(void)
{
    return be()->AdapterName();
}

bool rhi_DeviceLost(void)
{
    return s_up && be()->DeviceLost();
}

bool rhi_ResizeSwapchain(uint32_t width, uint32_t height, bool vsync)
{
    return be()->ResizeSwapchain(width, height, vsync);
}

RhiFormat rhi_SwapchainFormat(void)
{
    return be()->SwapchainFormat();
}

RhiTexture rhi_AcquireBackbuffer(void)
{
    return be()->AcquireBackbuffer();
}

void rhi_Present(void)
{
    be()->Present();
}

RhiBuffer rhi_CreateBuffer(const RhiBufferDesc *desc)
{
    return be()->CreateBuffer(desc);
}

void rhi_DestroyBuffer(RhiBuffer b)
{
    be()->DestroyBuffer(b);
}

void *rhi_MapBuffer(RhiBuffer b)
{
    return be()->MapBuffer(b);
}

void rhi_UnmapBuffer(RhiBuffer b)
{
    be()->UnmapBuffer(b);
}

RhiTexture rhi_CreateTexture(const RhiTextureDesc *desc)
{
    return be()->CreateTexture(desc);
}

void rhi_DestroyTexture(RhiTexture t)
{
    be()->DestroyTexture(t);
}

RhiSampler rhi_CreateSampler(const RhiSamplerDesc *desc)
{
    return be()->CreateSampler(desc);
}

void rhi_DestroySampler(RhiSampler s)
{
    be()->DestroySampler(s);
}

RhiShader rhi_CreateShader(const RhiShaderDesc *desc)
{
    return be()->CreateShader(desc);
}

void rhi_DestroyShader(RhiShader s)
{
    be()->DestroyShader(s);
}

RhiBindGroupLayout rhi_CreateBindGroupLayout(const RhiBindGroupLayoutDesc *desc)
{
    return be()->CreateBindGroupLayout(desc);
}

void rhi_DestroyBindGroupLayout(RhiBindGroupLayout l)
{
    be()->DestroyBindGroupLayout(l);
}

RhiBindGroup rhi_CreateBindGroup(const RhiBindGroupDesc *desc)
{
    return be()->CreateBindGroup(desc);
}

RhiPipeline rhi_CreatePipeline(const RhiPipelineDesc *desc)
{
    return be()->CreatePipeline(desc);
}

void rhi_DestroyPipeline(RhiPipeline p)
{
    be()->DestroyPipeline(p);
}

RhiCommandList rhi_BeginCommands(void)
{
    return be()->BeginCommands();
}

void rhi_EndCommands(RhiCommandList cl)
{
    be()->EndCommands(cl);
}

void rhi_Submit(RhiCommandList cl)
{
    be()->Submit(cl);
}

void rhi_WaitFrame(void)
{
    be()->WaitFrame();
}

void rhi_WaitIdle(void)
{
    be()->WaitIdle();
}

void rhi_CmdBarrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    be()->CmdBarrier(cl, barriers, count);
}

void rhi_CmdBeginRenderPass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    be()->CmdBeginRenderPass(cl, pass);
}

void rhi_CmdEndRenderPass(RhiCommandList cl)
{
    be()->CmdEndRenderPass(cl);
}

void rhi_CmdSetViewport(RhiCommandList cl, const RhiViewport *vp)
{
    be()->CmdSetViewport(cl, vp);
}

void rhi_CmdSetScissor(RhiCommandList cl, const RhiRect *rect)
{
    be()->CmdSetScissor(cl, rect);
}

void rhi_CmdSetPipeline(RhiCommandList cl, RhiPipeline p)
{
    be()->CmdSetPipeline(cl, p);
}

void rhi_CmdSetBindGroup(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    be()->CmdSetBindGroup(cl, group, bg);
}

void rhi_CmdSetVertexBuffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    be()->CmdSetVertexBuffer(cl, binding, b, offset);
}

void rhi_CmdSetIndexBuffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    be()->CmdSetIndexBuffer(cl, b, offset, u32);
}

void rhi_CmdSetStencilRef(RhiCommandList cl, uint8_t ref)
{
    be()->CmdSetStencilRef(cl, ref);
}

void rhi_CmdSetBlendConstant(RhiCommandList cl, const float rgba[4])
{
    be()->CmdSetBlendConstant(cl, rgba);
}

void rhi_CmdDraw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                 uint32_t instanceCount)
{
    be()->CmdDraw(cl, vertexCount, firstVertex, instanceCount);
}

void rhi_CmdDrawIndexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                        int32_t vertexOffset, uint32_t instanceCount)
{
    be()->CmdDrawIndexed(cl, indexCount, firstIndex, vertexOffset, instanceCount);
}

void rhi_CmdCopyBuffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                       uint64_t dstOffset, uint64_t size)
{
    be()->CmdCopyBuffer(cl, src, srcOffset, dst, dstOffset, size);
}

void rhi_CmdCopyBufferToTexture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    be()->CmdCopyBufferToTexture(cl, src, srcOffset, rowPitch, dst, mip, region);
}

void rhi_CmdCopyTexture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                        int32_t dstX, int32_t dstY)
{
    be()->CmdCopyTexture(cl, src, srcRegion, dst, dstX, dstY);
}

void rhi_CmdCopyTextureToBuffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                uint32_t rowPitch)
{
    be()->CmdCopyTextureToBuffer(cl, src, aspect, region, dst, dstOffset, rowPitch);
}

void rhi_CmdBeginLabel(RhiCommandList cl, const char *name)
{
    be()->CmdBeginLabel(cl, name);
}

void rhi_CmdEndLabel(RhiCommandList cl)
{
    be()->CmdEndLabel(cl);
}

bool rhi_ReadbackTexture(RhiTexture t, RhiViewAspect aspect, void *dst, size_t dstSize,
                         uint32_t *outRowPitch)
{
    return be()->ReadbackTexture(t, aspect, dst, dstSize, outRowPitch);
}

void rhi_GetStats(RhiStats *out)
{
    be()->GetStats(out);
}

bool rhi_TimestampsSupported(void)
{
    return be()->TimestampsSupported();
}

void rhi_CmdWriteTimestamp(RhiCommandList cl, uint32_t index)
{
    be()->CmdWriteTimestamp(cl, index);
}

uint32_t rhi_ReadTimestamps(uint64_t *ns, uint32_t max)
{
    return be()->ReadTimestamps(ns, max);
}

void rhi_PreferMailbox(bool on)
{
    be()->PreferMailbox(on);
}

bool rhi_PresentMailbox(void)
{
    return be()->PresentMailbox();
}

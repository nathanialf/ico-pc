/* d3d12_internal.h: shared state of the D3D12 backend of port/rhi/rhi.h.
 * port/rhi/d3d12/README.md describes the design.
 *
 * C through the COM C interfaces (COBJMACROS: ID3D12Device_CreateX(dev,
 * ...)); WIDL_C_INLINE_WRAPPERS gives the methods that return structures
 * (GetCPUDescriptorHandleForHeapStart, GetDesc) their C wrappers in
 * mingw-w64's headers. */
#ifndef PORT_RHI_D3D12_D3D12_INTERNAL_H
#define PORT_RHI_D3D12_D3D12_INTERNAL_H

#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIDL_C_INLINE_WRAPPERS
#define WIDL_C_INLINE_WRAPPERS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdio.h>

#include "../rhi.h"
#include "d3d12_enums.h"
#include "d3d12_plan.h"

/* --------------------------------------------------------------- objects */
typedef struct DxBuffer {
    ID3D12Resource *res;
    uint64_t size;
    RhiMemory kind;
    void *mapped;
    D3D12_GPU_VIRTUAL_ADDRESS gpu;
    D3dpBufferTrack track; /* RHI_MEM_DEVICE only */
} DxBuffer;

typedef struct DxTexture {
    ID3D12Resource *res;
    RhiFormat rhiFormat;
    DXGI_FORMAT resFormat; /* typeless, or typed for swapchain buffers */
    uint32_t width, height, mips, usage;
    uint32_t rtv;        /* RTV heap slot + 1; 0 = none */
    uint32_t dsv;        /* DSV heap slot + 1 (writable) */
    uint32_t dsvRO;      /* DSV heap slot + 1 (read-only depth and stencil) */
    uint32_t state;      /* tracked D3D12 state, in recording order */
    uint64_t copySerial; /* d3dp_CopyNeedsSync mark */
    bool swapchain;
} DxTexture;

typedef struct DxSampler {
    D3D12_SAMPLER_DESC desc;
    uint32_t heapIndex; /* its persistent descriptor in the sampler heap */
    bool dead;          /* destroyed; released when its frame slot is recycled */
} DxSampler;

#define DX_MAX_SIG 16

typedef struct DxShader {
    void *code;
    size_t size;
    RhiShaderStage stage;
    D3dpSigElem sig[DX_MAX_SIG];
    int sigCount; /* vertex shaders: input signature elements; -1 unread */
} DxShader;

typedef struct DxLayout {
    D3dpLayout l;
} DxLayout;

#define DX_MAX_ROOTSIGS 64

typedef struct DxRootSig {
    ID3D12RootSignature *rs;
    uint32_t layoutCount;
    uint32_t layoutIds[RHI_MAX_BIND_SLOTS];
    int8_t resParam[RHI_MAX_BIND_SLOTS], smpParam[RHI_MAX_BIND_SLOTS];
} DxRootSig;

typedef struct DxPipeline {
    ID3D12PipelineState *pso;
    DxRootSig *root;
    D3D_PRIMITIVE_TOPOLOGY topo;
    uint32_t strides[RHI_MAX_VERTEX_ATTRS]; /* per vertex buffer binding */
    uint32_t bindingMask;
} DxPipeline;

/* A transient bind group: its tables in the frame's descriptor rings. */
typedef struct DxBindGroup {
    uint32_t layoutId;
    D3D12_GPU_DESCRIPTOR_HANDLE res, smp;
    bool hasRes, hasSmp;
} DxBindGroup;

/* --------------------------------------------------------- per frame slot */
#define DX_MAX_CMD_LISTS 8
#define DX_RES_HEAP_SIZE (2u * 65536u)
#define DX_SMP_HEAP_SIZE 2048u           /* D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE */
#define DX_SMP_PERSISTENT 256u           /* one per RhiSampler: the sampler pool's capacity */
#define DX_SMP_DEFAULT DX_SMP_PERSISTENT /* point/clamp, for null tables */
#define DX_SMP_RESERVED (DX_SMP_PERSISTENT + 1u)
#define DX_MAX_LAYOUTS 64u
#define DX_RTV_SLOTS 8192u
#define DX_DSV_SLOTS 2048u

typedef struct DxCmdList {
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *cl;
    bool recording, submitted;
    uint64_t serial; /* unique per recording, never 0 */
    /* draw-time state */
    DxPipeline *pipeline;
    ID3D12RootSignature *rootSet;
    D3D_PRIMITIVE_TOPOLOGY topoSet;
    uint32_t groups[RHI_MAX_BIND_SLOTS]; /* bind group ids */
    uint32_t groupDirty;
    D3D12_GPU_VIRTUAL_ADDRESS vbAddr[RHI_MAX_VERTEX_ATTRS];
    uint32_t vbSize[RHI_MAX_VERTEX_ATTRS];
    uint32_t vbDirty;
    bool inPass;
} DxCmdList;

typedef struct DxFrame {
    uint64_t fenceValue; /* fence value of the frame's last submit */
    DxCmdList lists[DX_MAX_CMD_LISTS];
    uint32_t listCount;
    DxBindGroup *groups;
    uint32_t groupCount, groupCap;
    D3dpRing resRing, smpRing;
    IUnknown **garbage;
    uint32_t garbageCount, garbageCap;
    uint32_t deadSamplers[DX_SMP_PERSISTENT]; /* sampler ids destroyed in this frame */
    uint32_t deadSamplerCount;
    uint32_t nullGroups[DX_MAX_LAYOUTS]; /* per layout slot: a bind group of null views */
} DxFrame;

/* -------------------------------------------------------------- device */
typedef struct DxState {
    bool initialised;
    RhiLimits limits;
    char adapterName[128];

    HMODULE d3d12Dll, dxgiDll;
    PFN_D3D12_CREATE_DEVICE createDevice;
    PFN_D3D12_GET_DEBUG_INTERFACE getDebugInterface;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serializeRootSignature;

    IDXGIFactory4 *factory;
    IDXGIAdapter1 *adapter;
    bool warp;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12InfoQueue *info; /* debug layer present */
    uint32_t debugErrors;
    bool debugLayer;

    ID3D12Fence *fence;
    uint64_t fenceValue; /* last value signalled */
    HANDLE fenceEvent;

    /* descriptor heaps */
    ID3D12DescriptorHeap *resHeap, *smpHeap, *rtvHeap, *dsvHeap;
    uint32_t resInc, smpInc, rtvInc, dsvInc;
    D3D12_CPU_DESCRIPTOR_HANDLE resCpu, smpCpu, rtvCpu, dsvCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE resGpu, smpGpu;
    D3dpSlots rtvSlots, dsvSlots;

    /* frames */
    DxFrame frames[RHI_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;
    uint64_t listSerial;

    /* objects */
    D3dpPool buffers, textures, samplers, shaders, layouts, pipelines;
    DxRootSig roots[DX_MAX_ROOTSIGS];
    uint32_t rootCount;

    /* one-shot list for readback */
    ID3D12CommandAllocator *oneShotAlloc;
    ID3D12GraphicsCommandList *oneShotList;

    /* swapchain (absent when headless) */
    void *window;
    HWND hwnd;
    IDXGISwapChain3 *swapchain;
    uint32_t swapWidth, swapHeight;
    bool vsync, tearing;
    uint32_t swapCount;
    uint32_t swapTextures[4];
    uint32_t swapIndex;
    bool swapAcquired;
} DxState;

extern DxState g_dx;

#define DX_LOG(...)                                                                                \
    do {                                                                                           \
        fprintf(stderr, "rhi_d3d12: " __VA_ARGS__);                                                \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define DX_CHECK(expr) dx_Check((expr), #expr, __FILE__, __LINE__)

bool dx_Check(HRESULT hr, const char *what, const char *file, int line);
/* Prints and counts the debug layer's stored messages (errors and
 * corruption count toward rhi_d3d12_DebugErrorCount). */
void dx_DrainMessages(void);

static inline DxFrame *dx_CurFrame(void)
{
    return &g_dx.frames[g_dx.frameIndex % RHI_FRAMES_IN_FLIGHT];
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE dx_Cpu(D3D12_CPU_DESCRIPTOR_HANDLE base, uint32_t inc,
                                                 uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = {base.ptr + (SIZE_T)inc * index};
    return h;
}

static inline D3D12_GPU_DESCRIPTOR_HANDLE dx_Gpu(D3D12_GPU_DESCRIPTOR_HANDLE base, uint32_t inc,
                                                 uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = {base.ptr + (UINT64)inc * index};
    return h;
}

/* d3d12_resource.c */
void dx_Defer(IUnknown *obj);
void dx_DestroyGarbage(DxFrame *f);
DxBuffer *dx_GetBuffer(RhiBuffer b);
DxTexture *dx_GetTexture(RhiTexture t);
uint32_t dx_RegisterSwapchainBuffer(ID3D12Resource *res, uint32_t w, uint32_t h);
void dx_ReleaseSwapchainBuffer(uint32_t id);
void dx_ReleaseAllObjects(void);
/* d3d12_pipeline.c */
DxBindGroup *dx_GetBindGroup(uint32_t id);
/* This frame's bind group of null views for a layout (for a root parameter
 * the caller left unset); 0 on failure. */
uint32_t dx_NullBindGroup(uint32_t layoutId);
void dx_ReleaseRootSignatures(void);
/* d3d12_cmd.c */
bool dx_FramesInit(void);
void dx_FramesShutdown(void);
DxCmdList *dx_GetCmd(RhiCommandList cl);
void dx_WaitFence(uint64_t value);
uint64_t dx_Signal(void);
void dx_Transition(ID3D12GraphicsCommandList *cl, ID3D12Resource *res, uint32_t before,
                   uint32_t after);
/* d3d12_swapchain.c */
bool dx_SwapchainCreate(uint32_t w, uint32_t h, bool vsync);
void dx_SwapchainDestroy(void);

#endif /* PORT_RHI_D3D12_D3D12_INTERNAL_H */

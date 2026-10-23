/* d3d11_internal.h: shared state of the Direct3D 11 backend of
 * port/rhi/rhi.h (issue 126; compile-only in v0.8.6: it has not run on any
 * Windows machine).
 *
 * The contract between the backend's files: d3d11_device.c (runtime
 * loading, the device, frames, submission, timestamps, the shader cache's
 * entry points), d3d11_resource.c (buffers and their upload shadows,
 * textures, samplers, shaders, bind groups, deferred releases, readback),
 * d3d11_swapchain.c (the DXGI flip-model swapchain), d3d11_pipeline.c
 * (pipelines and their state objects) and d3d11_cmd.c (the rhi_cmd_*
 * recording on the deferred contexts).  docs/port/RHI.md, "Direct3D 11".
 *
 * C through the COM C interfaces (COBJMACROS: ID3D11Device_CreateX(dev,
 * ...)), as d3d12_internal.h.  Feature level 11_0 with the Direct3D 11.1
 * interfaces (ID3D11Device1, ID3D11DeviceContext1: Windows 8 and later);
 * rhi_init refuses a device without them. */
#ifndef PORT_RHI_D3D11_D3D11_INTERNAL_H
#define PORT_RHI_D3D11_D3D11_INTERNAL_H
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
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include "../rhi.h"
#include "../d3d12/d3d12_plan.h" /* the pools, layouts, the DXBC input signature reader */
#include "d3d11_shader.h"

/* ---------------------------------------------------------------- formats
 * As the D3D12 backend (d3d12_enums.h): textures are typeless resources
 * viewed typed, so RGBA8 UNORM and UINT copy between each other; the
 * swapchain's buffer is typed.  mapTexelBytes: the bytes a texel takes in
 * a Map of a staging copy of the whole resource (D32S8: the depth float,
 * the stencil byte and 24 unused bits); texelBytes: what a readback hands
 * back per texel (depth: the float). */
typedef struct D11FormatMap {
    bool set;
    DXGI_FORMAT resource; /* the format the resource is created with */
    DXGI_FORMAT view;     /* SRV / RTV format */
    DXGI_FORMAT dsv;      /* DSV format (depth formats) */
    uint32_t texelBytes;  /* bytes per texel of the copy plane (depth: plane 0; BC: per block) */
    uint32_t mapTexelBytes;
    bool depth, stencil;
    bool isInteger; /* blending not allowed */
} D11FormatMap;

static const D11FormatMap d11_formatMap[RHI_FMT_COUNT] = {
    [RHI_FMT_UNKNOWN] = {true, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 0, 0,
                         false, false, false},
    [RHI_FMT_RGBA8_UNORM] = {true, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
                             DXGI_FORMAT_UNKNOWN, 4, 4, false, false, false},
    [RHI_FMT_RGBA8_UINT] = {true, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT,
                            DXGI_FORMAT_UNKNOWN, 4, 4, false, false, true},
    [RHI_FMT_R8_UNORM] = {true, DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_UNKNOWN,
                          1, 1, false, false, false},
    [RHI_FMT_R8_UINT] = {true, DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_UNKNOWN, 1,
                         1, false, false, true},
    [RHI_FMT_R16_UINT] = {true, DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_UNKNOWN,
                          2, 2, false, false, true},
    [RHI_FMT_RGBA16F] = {true, DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         DXGI_FORMAT_UNKNOWN, 8, 8, false, false, false},
    [RHI_FMT_D32F] = {true, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT,
                      4, 4, true, false, false},
    [RHI_FMT_D32F_S8] = {true, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                         DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 4, 8, true, true, false},
    [RHI_FMT_BGRA8_UNORM] = {true, DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM,
                             DXGI_FORMAT_UNKNOWN, 4, 4, false, false, false},
    [RHI_FMT_BC1_UNORM] = {true, DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM,
                           DXGI_FORMAT_UNKNOWN, 8, 8, false, false, false},
    [RHI_FMT_BC2_UNORM] = {true, DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM,
                           DXGI_FORMAT_UNKNOWN, 16, 16, false, false, false},
    [RHI_FMT_BC3_UNORM] = {true, DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM,
                           DXGI_FORMAT_UNKNOWN, 16, 16, false, false, false},
    [RHI_FMT_BC7_UNORM] = {true, DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM,
                           DXGI_FORMAT_UNKNOWN, 16, 16, false, false, false},
    [RHI_FMT_R32_UINT] = {true, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_UNKNOWN,
                          4, 4, false, false, true},
    [RHI_FMT_RGBA32_UINT] = {true, DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_UINT,
                             DXGI_FORMAT_UNKNOWN, 16, 16, false, false, true},
};

/* ---------------------------------------------------------------- buffers
 * D3D11 binds a buffer by the flags it was made with, and a constant
 * buffer's bind flag excludes every other; a structured buffer (the
 * StructuredBuffer<float4> the VU vertex shaders read, stride 16) cannot
 * carry the vertex or index flag.  So one RHI buffer is up to three D3D11
 * buffers, one per bind class its usage asks for, each the whole size:
 *   D11_PART_VI   VERTEX | INDEX (and the plain copy buffer of a usage
 *                 with no bind class: COPY_SRC/COPY_DST only)
 *   D11_PART_CB   UNIFORM (read through VSSetConstantBuffers1's windows)
 *   D11_PART_SRV  STORAGE_READ (structured SRVs over a range)
 * A write reaches every part (the CPU's upload, a copy into the buffer).
 *
 * RHI_MEM_UPLOAD: rhi_map_buffer hands back `shadow`, CPU memory with a
 * stable address; the parts are DEFAULT buffers.  The command path notes
 * the byte ranges a list reads from each upload buffer (d11_cmd_touch), and
 * rhi_submit writes those ranges from the shadow into every part with
 * UpdateSubresource1 on the immediate context before the list executes
 * (d11_buffer_flush), so a list sees what the CPU wrote before its submit.
 * RHI_MEM_DEVICE: DEFAULT parts, filled by copies.  RHI_MEM_READBACK: a
 * STAGING buffer (`staging`) a copy writes and the shadow rhi_map_buffer
 * returns, refreshed from the staging buffer or texture at the map. */
enum { D11_PART_VI = 0, D11_PART_CB = 1, D11_PART_SRV = 2, D11_PART_COUNT = 3 };

/* structured SRVs a storage buffer keeps per range (round robin; the one
 * pushed out is released through d11_defer) */
#define D11_SRV_CACHE 8

typedef struct D11SrvCacheEntry {
    uint32_t first, count; /* elements of 16 bytes */
    ID3D11ShaderResourceView *srv;
} D11SrvCacheEntry;

typedef struct D11Buffer {
    uint64_t size; /* the asked size rounded up to 256 */
    uint32_t usage;
    RhiMemory kind;
    ID3D11Buffer *part[D11_PART_COUNT];
    uint8_t *shadow; /* UPLOAD and READBACK: the CPU copy rhi_map_buffer returns */
    /* READBACK: the STAGING buffer copies into the buffer land in
     * (stagingDirty: one landed since the last map), and the staging
     * texture of rhi_cmd_copy_texture_to_buffer (d11_readback_texture_target)
     * with where its rows go in the shadow */
    ID3D11Buffer *staging;
    bool stagingDirty;
    ID3D11Texture2D *stagingTex;
    bool texPending;
    uint32_t texW, texH, texTexelBytes, texMapTexelBytes, texRowPitch;
    uint64_t texDstOffset;
    D11SrvCacheEntry srvs[D11_SRV_CACHE];
    uint32_t srvNext;
} D11Buffer;

/* --------------------------------------------------------------- textures */
typedef struct D11Texture {
    ID3D11Texture2D *res;
    RhiFormat rhiFormat;
    DXGI_FORMAT resFormat; /* typeless, or typed for the swapchain's buffer */
    uint32_t width, height, mips, usage;
    uint32_t samples; /* 1, or the multisampled count */
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;   /* writable */
    ID3D11DepthStencilView *dsvRO; /* read-only depth and stencil */
    ID3D11ShaderResourceView *srv; /* RHI_TEX_SAMPLED: colour, or the depth plane */
    bool swapchain;
} D11Texture;

typedef struct D11Sampler {
    ID3D11SamplerState *state;
} D11Sampler;

/* ---------------------------------------------------------------- shaders
 * rhi_create_shader records the description (the HLSL file, the entry,
 * the macros and the table of embedded sources); the first pipeline that
 * needs the shader compiles it on its worker thread (d11_shader_compiled:
 * d3d11_shader_compile, the DXBC cache, CreateVertexShader or
 * CreatePixelShader), once: a failure is remembered and not retried. */
#define D11_MAX_SIG 16

typedef struct D11Shader {
    RhiShaderDesc desc; /* its strings and sources point at the copies below */
    char entry[64], file[64], defines[256], name[96];
    RhiShaderSource *sources; /* malloc'd copy of the array (the texts are the caller's) */
    SRWLOCK lock;             /* d11_shader_compiled; zero is SRWLOCK_INIT */
    bool compiled, failed;
    char error[256]; /* the failure's message, for every pipeline that asks again */
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    /* vertex shaders: the DXBC (CreateInputLayout takes it per pipeline)
     * and its input signature (d3dp_read_input_signature) */
    void *dxbc;
    size_t dxbcSize;
    D3dpSigElem sig[D11_MAX_SIG];
    int sigCount;
} D11Shader;

/* ------------------------------------------------------------ bind groups
 * A layout is the D3D12 plan's (d3dp_layout_build validates it: one slot
 * per register, at most RHI_MAX_DYNAMIC_OFFSETS dynamic uniforms); D3D11
 * uses only its slots.  Group g's slots bind to their own register numbers
 * (b<slot>, t<slot>, s<slot>; the shaders' flat registers, reg.hlsli
 * ICO_REG under ICO_D3D11) in the stages the slot names.
 *
 * A bind group is transient: its entries live in the frame slot's arena
 * until the slot is recycled.  It holds raw pointers to the views and
 * buffers (no reference: a destroyed object is released with the frame
 * slot's garbage, after the GPU is done). */
typedef struct D11Layout {
    D3dpLayout l;
} D11Layout;

#define D11_NO_DYN 0xFFu

typedef struct D11BindEntry {
    uint8_t slot;     /* the register number */
    uint8_t type;     /* RhiBindType */
    uint8_t stages;   /* (1 << RhiShaderStage) bits */
    uint8_t dynIndex; /* rank among the layout's dynamic uniforms, D11_NO_DYN otherwise */
    /* uniforms (static and dynamic): the CB part and its window in
     * 16-byte constants, firstConstant a multiple of 16 (256 bytes) and
     * numConstants a multiple of 16; a dynamic one adds its bind-time
     * offset / 16 to firstConstant */
    ID3D11Buffer *cb;
    uint32_t firstConstant, numConstants;
    /* the RhiBuffer the entry reads (uniforms, storage), 0 for none, and the
     * byte range [lo, hi) it reads (a dynamic one: before its offset): what
     * the command path passes to d11_cmd_touch at the bind */
    uint32_t buffer;
    uint64_t lo, hi;
    ID3D11ShaderResourceView *srv; /* textures, storage buffers; NULL reads zeros */
    ID3D11SamplerState *smp;       /* samplers; the default (point, clamp) when unbound */
} D11BindEntry;

typedef struct D11BindGroup {
    uint32_t layoutId;
    uint32_t count; /* entries, in the layout's slot order */
    uint32_t dynCount;
    D11BindEntry e[D3DP_MAX_SLOTS];
} D11BindGroup;

/* -------------------------------------------------------------- pipelines
 * d3d11_pipeline.c (D3D11-CMD) fills and reads this; the device files only
 * size the pool and release it at shutdown (d11_pipeline_release). */
typedef struct D11Pipeline {
    ID3D11VertexShader *vs; /* referenced (AddRef) from the shaders */
    ID3D11PixelShader *ps;
    ID3D11InputLayout *il; /* NULL for a pipeline without vertex inputs */
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *ds;
    ID3D11RasterizerState *rs;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    uint32_t strides[RHI_MAX_VERTEX_ATTRS]; /* per vertex buffer binding */
    uint32_t bindingMask;
    uint32_t layoutCount;
    uint32_t layoutIds[RHI_MAX_BIND_SLOTS];
    uint32_t colorCount;
    RhiFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    RhiFormat depthFormat;
    uint32_t samples;
} D11Pipeline;

/* ---------------------------------------------------------- command lists
 * One deferred context per list slot of a frame slot, made at rhi_init and
 * reused: rhi_begin_commands hands out the next, rhi_end_commands calls
 * FinishCommandList (the deferred context's state is cleared), rhi_submit
 * flushes the upload ranges the list touched, ExecuteCommandList on the
 * immediate context (in submit order) and ends the frame slot's event
 * query.  The handle id is (frame tag << 20) | (list index + 1), as the
 * D3D12 backend's. */
#define D11_MAX_CMD_LISTS 8

/* the bytes [lo, hi) of upload buffer `buffer` a list reads */
typedef struct D11Touch {
    uint32_t buffer;
    uint64_t lo, hi;
} D11Touch;

/* The draw-time state of a list: d3d11_cmd.c's (D3D11-CMD may add to it);
 * zeroed by rhi_begin_commands. */
typedef struct D11CmdState {
    uint32_t pipeline; /* bound RhiPipeline id, 0 none */
    uint32_t groups[RHI_MAX_BIND_SLOTS];
    uint32_t offsets[RHI_MAX_BIND_SLOTS][RHI_MAX_DYNAMIC_OFFSETS];
    uint32_t groupDirty;
    uint32_t vb[RHI_MAX_VERTEX_ATTRS]; /* RhiBuffer ids */
    uint64_t vbOffset[RHI_MAX_VERTEX_ATTRS];
    uint32_t vbDirty;
    uint32_t ib;
    uint64_t ibOffset;
    bool ib32;
    uint8_t stencilRef;
    float blendConstant[4];
    bool inPass;
    /* the textures bound as the pass's targets (ids; 0 none): their SRVs
     * are unbound before they become targets */
    uint32_t targets[RHI_MAX_COLOR_TARGETS];
    uint32_t depthTarget;
    uint32_t passW, passH;
} D11CmdState;

typedef struct D11CommandList {
    ID3D11DeviceContext *ctx;   /* the deferred context */
    ID3D11DeviceContext1 *ctx1; /* its 11.1 interface (constant buffer windows, DiscardView) */
    ID3DUserDefinedAnnotation *annot; /* labels; NULL without the debug layer */
    ID3D11CommandList *list;          /* FinishCommandList's result until the slot is recycled */
    bool recording, submitted;
    bool finishFailed; /* FinishCommandList failed: nothing to execute */
    uint64_t serial;   /* unique per recording, never 0 */
    D11Touch *touch;   /* upload ranges read (d11_cmd_touch); kept across recordings */
    uint32_t touchCount, touchCap;
    uint32_t tsMask[RHI_TIMESTAMP_WORDS]; /* timestamp indices this list wrote */
    D11CmdState st;
} D11CommandList;

/* --------------------------------------------------------- per frame slot */
#define D11_SUBMIT_HISTORY 8

typedef struct D11Frame {
    D11CommandList lists[D11_MAX_CMD_LISTS];
    uint32_t listCount;
    /* D3D11_QUERY_EVENT, ended on the immediate context after each submit
     * of the slot (the last end counts): rhi_wait_frame waits on it when
     * the slot comes round again */
    ID3D11Query *event;
    bool eventIssued;
    D11BindGroup *groups;
    uint32_t groupCount, groupCap;
    IUnknown **garbage; /* released when the slot is recycled */
    uint32_t garbageCount, garbageCap;
    /* GPU timestamps: a TIMESTAMP_DISJOINT query begun at the slot's first
     * submit and ended when rhi_wait_frame leaves it, and RHI_MAX_TIMESTAMPS
     * TIMESTAMP queries the lists end; the indices the executed lists wrote */
    ID3D11Query *tsDisjoint;
    bool tsDisjointBegun;
    ID3D11Query *ts[RHI_MAX_TIMESTAMPS];
    uint32_t tsWritten[RHI_TIMESTAMP_WORDS];
} D11Frame;

/* ------------------------------------------------------------------ device */
typedef struct D11State {
    bool initialised;
    RhiLimits limits;
    char adapterName[128];

    HMODULE d3d11Dll, dxgiDll;
    PFN_D3D11_CREATE_DEVICE createDevice;

    IDXGIFactory2 *factory;
    IDXGIAdapter1 *adapter; /* NULL on WARP (D3D_DRIVER_TYPE_WARP) */
    bool warp;
    uint32_t vendorId, deviceId;
    ID3D11Device *device;
    ID3D11Device1 *device1;
    ID3D11DeviceContext *ctx; /* immediate */
    ID3D11DeviceContext1 *ctx1;
    D3D_FEATURE_LEVEL featureLevel;
    /* D3D11_FEATURE_D3D11_OPTIONS: partial constant buffer updates
     * (UpdateSubresource1 with a box on a constant buffer; without them a
     * flush writes the whole CB part) */
    bool cbPartialUpdate;
    /* D3D11_FEATURE_THREADING: the driver records command lists itself
     * (false: the runtime emulates them); for the summary line */
    bool driverCommandLists, driverConcurrentCreates;
    ID3D11InfoQueue *info; /* the debug layer's queue, when on */
    uint32_t debugErrors;
    bool debugLayer;
    /* the device was removed, reset or hung (d11_check): reason logged once,
     * submits and presents are skipped, rhi_device_lost reports it */
    bool deviceLost;
    uint32_t lostFailures;

    struct {
        uint64_t serial;
        uint32_t replay, frame;
    } submits[D11_SUBMIT_HISTORY];

    uint32_t submitCount;
    uint32_t tagReplay, tagFrame;

    /* frames */
    D11Frame frames[RHI_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;
    uint64_t listSerial;
    ID3D11Query *idleEvent; /* rhi_wait_idle's own event */

    /* objects */
    D3dpPool buffers, textures, samplers, shaders, layouts, pipelines;
    ID3D11SamplerState *defaultSampler; /* point, clamp: an unbound sampler slot */

    /* the DXBC cache (rhi_set_pipeline_cache_path): its folder and the
     * build the cache key carries; "" = no cache */
    char cacheDir[1024];
    char buildId[96];
    bool cacheStatsLogged;

    /* swapchain (absent when headless) */
    void *window;
    HWND hwnd;
    IDXGISwapChain1 *swapchain;
    IDXGISwapChain2 *swapchain2; /* the waitable object; NULL before DXGI 1.3 */
    bool flipDiscard;            /* FLIP_DISCARD (Windows 10), else FLIP_SEQUENTIAL */
    uint32_t swapWidth, swapHeight;
    bool vsync, tearing;
    uint64_t swapchains;
    uint32_t swapTexture; /* buffer 0's texture id (the flip model's back buffer) */
    bool swapAcquired;
    uint32_t frameLatency;
    HANDLE frameLatencyEvent;
    bool frameLatencyTimeoutLogged;
    bool frameLatencySkipWait;
    bool frameStatsValid;
    uint64_t frameStatsPresent, frameStatsRefresh, frameStatsSyncRefresh, frameStatsSyncNs;
    /* RhiStats: presents and the time in Present, waits that blocked and
     * their time, the acquire's wait */
    uint64_t presents, presentNs, fenceWaits, fenceWaitNs, acquireNs;

    /* GPU timestamps: on when every slot got its queries; the frequency the
     * last disjoint query gave; the last recycled slot's times in ns */
    bool timestamps;
    uint64_t tsResult[RHI_MAX_TIMESTAMPS];
    uint32_t tsCount;
    bool tsDisjointLogged;

    /* RhiStats counters kept where the Vulkan backend counts them */
    RhiStats counts;
} D11State;

extern D11State g_d11;

#define D11_LOG(...)                                                                               \
    do {                                                                                           \
        fprintf(stderr, "rhi_d3d11: " __VA_ARGS__);                                                \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define D11_CHECK(expr) d11_check((expr), #expr, __FILE__, __LINE__)
/* Releases a COM object if set and clears the pointer. */
#define D11_RELEASE(p)                                                                             \
    do {                                                                                           \
        if (p) {                                                                                   \
            IUnknown_Release((IUnknown *)(p));                                                     \
            (p) = NULL;                                                                            \
        }                                                                                          \
    } while (0)

/* d3d11_device.c */
bool d11_check(HRESULT hr, const char *what, const char *file, int line);
/* Prints and counts the debug layer's stored messages. */
void d11_drain_messages(void);
/* Ends `q` (an event query) on the immediate context and spins until the
 * GPU has passed it (Sleep(0)/SwitchToThread between polls); false on a
 * lost device.  Counted in RhiStats.fenceWaits/fenceWaitNs when it blocked. */
bool d11_wait_query(ID3D11Query *q);
/* The current frame slot's list for a handle, NULL for a stale one. */
D11CommandList *d11_get_cmd(RhiCommandList cl);
/* Notes that list c reads [lo, hi) of buffer id `buffer` (a no-op unless it
 * is an UPLOAD buffer): rhi_submit writes that range from the shadow into
 * the buffer's parts before the list executes.  The command path calls it
 * for every bind group range, vertex/index binding with its draw extent and
 * copy source it records on an upload buffer. */
void d11_cmd_touch(D11CommandList *c, uint32_t buffer, uint64_t lo, uint64_t hi);

static inline uint64_t d11_qpc_ns(uint64_t ticks)
{
    static LARGE_INTEGER s_freq;
    if (s_freq.QuadPart <= 0 && (!QueryPerformanceFrequency(&s_freq) || s_freq.QuadPart <= 0)) {
        return 0;
    }
    const uint64_t f = (uint64_t)s_freq.QuadPart;
    return ticks / f * 1000000000ull + ticks % f * 1000000000ull / f;
}

static inline uint64_t d11_now_ns(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return d11_qpc_ns((uint64_t)c.QuadPart);
}

static inline D11Frame *d11_cur_frame(void)
{
    return &g_d11.frames[g_d11.frameIndex % RHI_FRAMES_IN_FLIGHT];
}

/* d3d11_resource.c */
/* Releases obj when the current frame slot is recycled. */
void d11_defer(IUnknown *obj);
void d11_destroy_garbage(D11Frame *f);
D11Buffer *d11_get_buffer(RhiBuffer b);
D11Texture *d11_get_texture(RhiTexture t);
D11Shader *d11_get_shader(RhiShader s);
D11BindGroup *d11_get_bind_group(uint32_t id);
D11Layout *d11_get_layout(uint32_t id);
/* The part a read of the given RHI usage goes to (RHI_BUF_VERTEX/INDEX:
 * D11_PART_VI, RHI_BUF_UNIFORM: _CB, RHI_BUF_STORAGE_READ: _SRV), NULL when
 * the buffer has none; d11_buffer_any_part: the first part it has (a copy
 * source). */
ID3D11Buffer *d11_buffer_part(const D11Buffer *b, uint32_t part);
ID3D11Buffer *d11_buffer_any_part(const D11Buffer *b);
/* UPLOAD: writes [lo, hi) of the shadow into every part through ctx's
 * UpdateSubresource1 (16-byte aligned; a constant buffer whole when the
 * device has no partial updates). */
void d11_buffer_flush(D11Buffer *b, ID3D11DeviceContext1 *ctx, uint64_t lo, uint64_t hi);
/* A structured SRV (16-byte elements) over [offset, offset + size) of the
 * buffer's SRV part, from its cache; NULL when the range is invalid. */
ID3D11ShaderResourceView *d11_buffer_srv(D11Buffer *b, uint64_t offset, uint64_t size);
/* rhi_cmd_copy_texture_to_buffer into a READBACK buffer: the staging
 * texture (made or reused at region's size and src's format) the command
 * path copies the region into with CopySubresourceRegion, recording where
 * its rows land in the shadow at the next rhi_map_buffer (row r at
 * dstOffset + r * rowPitch).  NULL when the buffer is no readback buffer
 * or the rows do not fit. */
ID3D11Texture2D *d11_readback_texture_target(D11Buffer *dst, const D11Texture *src, uint32_t w,
                                             uint32_t h, uint64_t dstOffset, uint32_t rowPitch);
/* The swapchain's buffer 0 as an RHI texture (an RTV made here); 0 on
 * failure.  Release before ResizeBuffers. */
uint32_t d11_register_swapchain_buffer(ID3D11Texture2D *res, uint32_t w, uint32_t h);
void d11_release_swapchain_buffer(uint32_t id);
void d11_release_all_objects(void);
/* Names an object for the debug layer's messages (nothing without it). */
void d11_set_name(ID3D11DeviceChild *o, const char *name);
/* The shader compiled, once (any thread; serialised per shader): vs or ps
 * set, and a vertex shader's dxbc and sig.  false with the reason in err
 * (the same on every later call). */
bool d11_shader_compiled(D11Shader *s, char *err, size_t errn);

/* d3d11_pipeline.c (D3D11-CMD) */
D11Pipeline *d11_get_pipeline(uint32_t id);
/* releases every pipeline's objects and the state-object tables (shutdown) */
void d11_pipelines_release(void);

/* d3d11_swapchain.c */
bool d11_swapchain_create(uint32_t w, uint32_t h, bool vsync);
void d11_swapchain_destroy(void);

#endif /* PORT_RHI_D3D11_D3D11_INTERNAL_H */

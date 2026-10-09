/* rhi.h: the thin render hardware interface the backends implement.
 *
 * Backends: port/rhi/vk, port/rhi/d3d12 (Metal later).  A build may link
 * more than one (Windows: Vulkan and D3D12); rhi_create_backend at the end
 * picks the one the calls below go to (port/rhi/rhi_backend.h).
 * The surface is deliberately small: the game needs about a dozen shader
 * programs, under a hundred pipelines, about fifteen render targets, ring
 * buffers for per-frame vertex and uniform data, and no compute.
 *
 * Rules for additions:
 *  - Nothing Vulkan-only: no input attachments, no framebuffer feedback
 *    loops, no push descriptors.  Anything added must be expressible in
 *    D3D12 and Metal.
 *  - Resource state transitions are explicit (rhi_cmd_barrier).  The caller
 *    (rd_core) knows the frame graph; backends do not track state.
 *  - All handles are opaque 32-bit ids with a generation; 0 is null.
 *  - Byte layouts (vertex formats, uniform blocks) are fixed by rd_core
 *    and shared with the shaders through port/shaders/shader_consts.h and
 *    its HLSL twins (common.hlsli, vu_common.hlsli).
 */
#ifndef PORT_RHI_RHI_H
#define PORT_RHI_RHI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef RHI_BACKEND_PREFIX

/* compiling a backend: its rhi_* definitions get the backend's prefix */
#include "rhi_backend_names.h"

#endif
#ifdef __cplusplus

extern "C" {
#endif

/* ------------------------------------------------------------- handles */
typedef struct {
    uint32_t id;
} RhiBuffer;

typedef struct {
    uint32_t id;
} RhiTexture;

typedef struct {
    uint32_t id;
} RhiSampler;

typedef struct {
    uint32_t id;
} RhiShader;

typedef struct {
    uint32_t id;
} RhiPipeline;

typedef struct {
    uint32_t id;
} RhiBindGroupLayout;

typedef struct {
    uint32_t id;
} RhiBindGroup;

typedef struct {
    uint32_t id;
} RhiCommandList;

typedef struct {
    uint32_t id;
} RhiFence;

#define RHI_NULL_ID 0u
#define RHI_MAX_COLOR_TARGETS                                                                      \
    2 /* colour + dual-source helper never needs more than one target; 2 for puddle/pool copies */
#define RHI_MAX_BIND_SLOTS 8
#define RHI_MAX_VERTEX_ATTRS 8
#define RHI_FRAMES_IN_FLIGHT 2

/* ------------------------------------------------------------- formats */
typedef enum RhiFormat {
    RHI_FMT_UNKNOWN = 0,
    RHI_FMT_RGBA8_UNORM, /* colour targets, textures; alpha holds raw GS alpha (0x80 = 1.0) */
    RHI_FMT_RGBA8_UINT,  /* colour targets for the exact integer blend path (ping-pong passes) */
    RHI_FMT_R8_UNORM,    /* DATE snapshot, font atlas, fog LUT index textures */
    RHI_FMT_R8_UINT,
    RHI_FMT_R16_UINT,
    RHI_FMT_RGBA16F,
    RHI_FMT_D32F,        /* reversed-Z depth */
    RHI_FMT_D32F_S8,     /* reversed-Z depth + stencil (scene target).  The logical scene
                          * depth-stencil format: the Vulkan backend may back it with
                          * D24_UNORM_S8_UINT (24-bit depth) on a device without
                          * D32_SFLOAT_S8_UINT; see RhiLimits.depthStencilFormatName and
                          * depthReadback */
    RHI_FMT_BGRA8_UNORM, /* swapchain */
    /* Texture packs: block-compressed sampled textures (4x4 texel blocks,
     * 8 bytes a block for BC1, 16 for BC2/BC3/BC7), created only when
     * RhiLimits.bcTextures is true (rhi_create_texture refuses them
     * otherwise).  Copy-destination and sampled only, never targets. */
    RHI_FMT_BC1_UNORM,
    RHI_FMT_BC2_UNORM,
    RHI_FMT_BC3_UNORM,
    RHI_FMT_BC7_UNORM,
    RHI_FMT_COUNT
} RhiFormat;

/* The block-compressed formats above. */
static inline bool rhi_format_is_block(RhiFormat f)
{
    return f == RHI_FMT_BC1_UNORM || f == RHI_FMT_BC2_UNORM || f == RHI_FMT_BC3_UNORM ||
           f == RHI_FMT_BC7_UNORM;
}

typedef enum RhiVertexFormat {
    RHI_VTX_F32x1,
    RHI_VTX_F32x2,
    RHI_VTX_F32x3,
    RHI_VTX_F32x4,
    RHI_VTX_U8x4_UNORM,
    RHI_VTX_U8x4_UINT,
    RHI_VTX_U16x2_UINT,
    RHI_VTX_U32x1,
    RHI_VTX_COUNT
} RhiVertexFormat;

/* ------------------------------------------------------------ resources */
typedef enum RhiBufferUsage {
    RHI_BUF_VERTEX = 1 << 0,
    RHI_BUF_INDEX = 1 << 1,
    RHI_BUF_UNIFORM = 1 << 2,
    RHI_BUF_STORAGE_READ = 1 << 3, /* bone matrices, particle arrays */
    RHI_BUF_COPY_SRC = 1 << 4,
    RHI_BUF_COPY_DST = 1 << 5,
    RHI_BUF_READBACK = 1 << 6 /* host-visible, for frame dumps */
} RhiBufferUsage;

typedef enum RhiMemory {
    RHI_MEM_DEVICE = 0, /* static: meshes built once at load */
    RHI_MEM_UPLOAD = 1, /* per-frame ring: mapped persistently, written by the CPU */
    RHI_MEM_READBACK = 2
} RhiMemory;

typedef struct RhiBufferDesc {
    uint64_t size;
    uint32_t usage; /* RhiBufferUsage bits */
    RhiMemory memory;
    const char *debugName;
} RhiBufferDesc;

typedef enum RhiTextureUsage {
    RHI_TEX_SAMPLED = 1 << 0,
    RHI_TEX_RENDER_TARGET = 1 << 1,
    RHI_TEX_DEPTH_STENCIL = 1 << 2,
    RHI_TEX_COPY_SRC = 1 << 3,
    RHI_TEX_COPY_DST = 1 << 4
} RhiTextureUsage;

typedef struct RhiTextureDesc {
    uint32_t width, height;
    uint32_t mipLevels; /* 1 for the Original preset; generated mips only in Enhanced */
    RhiFormat format;
    uint32_t usage; /* RhiTextureUsage bits */
    const char *debugName;
} RhiTextureDesc;

/* Views: a depth-stencil texture is sampled either as depth (fog, DoF) or
 * never as stencil; an RGBA8 target can be viewed as RGB24-with-TA0 by the
 * shader, which is a shader concern, not a view.  So one view kind is enough
 * beyond the default: the depth aspect. */
typedef enum RhiViewAspect { RHI_ASPECT_COLOR = 0, RHI_ASPECT_DEPTH = 1 } RhiViewAspect;

typedef enum RhiFilter { RHI_FILTER_NEAREST = 0, RHI_FILTER_LINEAR = 1 } RhiFilter;

typedef enum RhiWrap { RHI_WRAP_REPEAT = 0, RHI_WRAP_CLAMP = 1 } RhiWrap;

typedef struct RhiSamplerDesc {
    RhiFilter mag, min, mip;
    RhiWrap s, t;
    float maxAnisotropy; /* 1.0 in Original */
    float lodBias;       /* Enhanced trilinear only; Original fixes the mip per texture */
    float minLod, maxLod;
} RhiSamplerDesc;

/* ---------------------------------------------------- resource states */
typedef enum RhiState {
    RHI_STATE_UNDEFINED = 0,
    RHI_STATE_RENDER_TARGET,
    RHI_STATE_DEPTH_WRITE,
    RHI_STATE_DEPTH_READ, /* depth test on, no write, also sampleable as depth; stencil is
                           * read-only too: a pass that writes stencil without writing depth
                           * (shadow volumes) uses DEPTH_WRITE with depthWrite = false */
    RHI_STATE_SHADER_READ,
    RHI_STATE_COPY_SRC,
    RHI_STATE_COPY_DST,
    RHI_STATE_PRESENT,
    RHI_STATE_COUNT
} RhiState;

typedef struct RhiTextureBarrier {
    RhiTexture texture;
    RhiState before, after;
} RhiTextureBarrier;

/* ------------------------------------------------------------- shaders */
typedef enum RhiShaderStage { RHI_STAGE_VERTEX = 0, RHI_STAGE_FRAGMENT = 1 } RhiShaderStage;

/* Shaders are precompiled at build time by DXC from one HLSL source to
 * SPIR-V (Vulkan) and DXIL (D3D12) and embedded as byte arrays; the
 * backend receives whichever blob it understands. */
typedef struct RhiShaderDesc {
    RhiShaderStage stage;
    const void *bytecode;
    size_t bytecodeSize;
    const char *entryPoint; /* the entry's name (IcoShaderBlob.entry); Vulkan only */
    const char *debugName;
} RhiShaderDesc;

/* --------------------------------------------------------- bind groups
 * Fixed slot model shared by all programs (see port/shaders/common.hlsli):
 *  group 0: per-frame uniforms   (b0)
 *  group 1: per-draw uniforms    (b1; the VU programs' b2, b3), the VU
 *           vertex stream (t0)
 *  group 2: textures t1..t4 and samplers s1..s4
 * Backends map these onto descriptor sets / root parameters / argument
 * buffers; rd_core never sees the mapping. */
typedef enum RhiBindType {
    RHI_BIND_UNIFORM_BUFFER = 0,
    RHI_BIND_STORAGE_BUFFER,
    RHI_BIND_SAMPLED_TEXTURE,
    RHI_BIND_SAMPLER,
    /* A uniform buffer whose offset is given when the group is
     * bound (rhi_cmd_set_bind_group_offsets), so one group serves every draw
     * that reads a block of the same size from the same buffer.  The
     * binding's offset is the base and its size (required) the range each
     * draw sees; base + bind-time offset + size must lie in the buffer.
     * Vulkan: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC.  D3D12: a root CBV
     * per slot.  Same b register
     * class as RHI_BIND_UNIFORM_BUFFER: the shader does not change. */
    RHI_BIND_UNIFORM_BUFFER_DYNAMIC,
    RHI_BIND_COUNT
} RhiBindType;

/* Dynamic uniform slots in one bind group layout, at most. */
#define RHI_MAX_DYNAMIC_OFFSETS 4

typedef struct RhiBindSlot {
    uint32_t slot;
    RhiBindType type;
    uint32_t stages; /* bitmask of (1 << RhiShaderStage) */
} RhiBindSlot;

typedef struct RhiBindGroupLayoutDesc {
    const RhiBindSlot *slots;
    uint32_t slotCount;
    const char *debugName;
} RhiBindGroupLayoutDesc;

typedef struct RhiBinding {
    uint32_t slot;
    RhiBindType type;
    RhiBuffer buffer;      /* for buffer types */
    uint64_t offset, size; /* buffer range; uniform ranges respect rhi_limits().uniformAlign;
                            * a dynamic uniform's base and per-draw range */
    RhiTexture texture;    /* for sampled textures */
    RhiViewAspect aspect;
    RhiSampler sampler; /* for samplers */
} RhiBinding;

typedef struct RhiBindGroupDesc {
    RhiBindGroupLayout layout;
    const RhiBinding *bindings;
    uint32_t bindingCount;
} RhiBindGroupDesc;

/* ------------------------------------------------------------ pipelines */
typedef enum RhiTopology {
    RHI_TOPO_TRIANGLE_LIST = 0,
    RHI_TOPO_TRIANGLE_STRIP, /* GS strips; rd_core emits restart-free strips per GIF packet */
    RHI_TOPO_LINE_LIST,
    RHI_TOPO_POINT_LIST,
    RHI_TOPO_COUNT
} RhiTopology;

typedef enum RhiCompare {
    RHI_CMP_NEVER = 0,
    RHI_CMP_LESS,
    RHI_CMP_EQUAL,
    RHI_CMP_LEQUAL,
    RHI_CMP_GREATER,
    RHI_CMP_NOTEQUAL,
    RHI_CMP_GEQUAL,
    RHI_CMP_ALWAYS,
    RHI_CMP_COUNT
} RhiCompare;

typedef enum RhiBlendFactor {
    RHI_BF_ZERO = 0,
    RHI_BF_ONE,
    RHI_BF_SRC_COLOR,
    RHI_BF_ONE_MINUS_SRC_COLOR,
    RHI_BF_DST_COLOR,
    RHI_BF_ONE_MINUS_DST_COLOR,
    RHI_BF_SRC_ALPHA,
    RHI_BF_ONE_MINUS_SRC_ALPHA,
    RHI_BF_DST_ALPHA,
    RHI_BF_ONE_MINUS_DST_ALPHA,
    RHI_BF_CONSTANT,
    RHI_BF_ONE_MINUS_CONSTANT,
    /* dual-source: the fragment shader's second output carries the GS blend
     * factor (As/128 or FIX/128), so alpha above 1.0 and the 0x80 scale are
     * exact.  Only with RhiLimits.dualSourceBlend (Vulkan dualSrcBlend,
     * optional; D3D12 always, Metal always). */
    RHI_BF_SRC1_COLOR,
    RHI_BF_ONE_MINUS_SRC1_COLOR,
    RHI_BF_SRC1_ALPHA,
    RHI_BF_ONE_MINUS_SRC1_ALPHA,
    RHI_BF_COUNT
} RhiBlendFactor;

typedef enum RhiBlendOp {
    RHI_BO_ADD = 0,
    RHI_BO_SUBTRACT,
    RHI_BO_REVERSE_SUBTRACT,
    RHI_BO_COUNT
} RhiBlendOp;

typedef struct RhiBlendState {
    bool enable;
    RhiBlendFactor srcColor, dstColor;
    RhiBlendOp colorOp;
    RhiBlendFactor srcAlpha, dstAlpha;
    RhiBlendOp alphaOp;
    uint8_t writeMask; /* bits 0..3 = R G B A */
} RhiBlendState;

typedef enum RhiStencilOp {
    RHI_SO_KEEP = 0,
    RHI_SO_ZERO,
    RHI_SO_REPLACE,
    RHI_SO_INCR_WRAP,
    RHI_SO_DECR_WRAP, /* shadow volume count: sign picks one */
    RHI_SO_INCR_CLAMP,
    RHI_SO_DECR_CLAMP,
    RHI_SO_INVERT,
    RHI_SO_COUNT
} RhiStencilOp;

typedef struct RhiStencilFace {
    RhiCompare compare;
    RhiStencilOp pass, fail, depthFail;
} RhiStencilFace;

typedef struct RhiDepthStencilState {
    bool depthTest;
    bool depthWrite;
    RhiCompare
        depthCompare; /* depth grows with GS Z: GS GEQUAL is RHI_CMP_GEQUAL (rd_pipeline.c) */
    bool stencilTest;
    uint8_t stencilReadMask, stencilWriteMask;
    RhiStencilFace front,
        back; /* both faces identical for the game; two-sided kept for the shadow pass */
} RhiDepthStencilState;

typedef struct RhiVertexAttr {
    uint32_t location;
    uint32_t binding;
    RhiVertexFormat format;
    uint32_t offset;
} RhiVertexAttr;

typedef struct RhiVertexBinding {
    uint32_t binding;
    uint32_t stride;
    bool perInstance;
} RhiVertexBinding;

typedef struct RhiPipelineDesc {
    RhiShader vertex, fragment;
    const RhiBindGroupLayout *layouts;
    uint32_t layoutCount;
    const RhiVertexBinding *vertexBindings;
    uint32_t vertexBindingCount;
    const RhiVertexAttr *vertexAttrs;
    uint32_t vertexAttrCount;
    RhiTopology topology;
    bool cullNone; /* the game never culls: GS has no culling; always true */
    RhiBlendState blend[RHI_MAX_COLOR_TARGETS];
    RhiDepthStencilState depthStencil;
    RhiFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    uint32_t colorCount;
    RhiFormat depthFormat; /* RHI_FMT_UNKNOWN for none */
    const char *debugName;
} RhiPipelineDesc;

/* ------------------------------------------------------------- passes */
typedef enum RhiLoadOp {
    RHI_LOAD_LOAD = 0,
    RHI_LOAD_CLEAR,
    RHI_LOAD_DONT_CARE,
    RHI_LOAD_COUNT
} RhiLoadOp;

/* What the pass leaves in an attachment.  STORE (0, the
 * default of a zeroed desc) keeps what the pass drew; DONT_CARE lets a
 * tile-based GPU skip writing the attachment back to memory, for contents
 * nothing reads afterwards (the attachment is then undefined).  A
 * read-only depth attachment (readOnlyDepth) is never stored whatever this
 * says.  D3D12 (no render pass objects) keeps the contents either way, a
 * valid DONT_CARE. */
typedef enum RhiStoreOp { RHI_STORE_STORE = 0, RHI_STORE_DONT_CARE, RHI_STORE_COUNT } RhiStoreOp;

typedef struct RhiColorAttachment {
    RhiTexture texture;
    RhiLoadOp load;
    float clear[4]; /* integer formats (RGBA8_UINT...): the integer value per channel, 0..255 */
    RhiStoreOp store;
} RhiColorAttachment;

typedef struct RhiDepthAttachment {
    RhiTexture texture; /* id 0 = no depth */
    RhiLoadOp depthLoad, stencilLoad;
    float clearDepth;
    uint8_t clearStencil;
    bool
        readOnlyDepth; /* depth test without write and the same texture bound for sampling is NOT allowed; use a copy */
    RhiStoreOp store;        /* the depth aspect */
    RhiStoreOp stencilStore; /* the stencil aspect (a format with one); zero stores it */
} RhiDepthAttachment;

typedef struct RhiRenderPassDesc {
    RhiColorAttachment color[RHI_MAX_COLOR_TARGETS];
    uint32_t colorCount;
    RhiDepthAttachment depth;
    uint32_t width, height;
} RhiRenderPassDesc;

typedef struct RhiViewport {
    float x, y, w, h, minDepth, maxDepth;
} RhiViewport;

typedef struct RhiRect {
    int32_t x, y;
    uint32_t w, h;
} RhiRect;

/* ------------------------------------------------------------- device */
typedef struct RhiLimits {
    uint32_t uniformAlign; /* 256 on D3D12, usually 64..256 on Vulkan: rd_core aligns to this */
    uint32_t maxTextureSize;
    bool dualSourceBlend; /* optional; without it rd blends in two passes */
    bool stencilWrap;     /* must be true */
    /* true when rhi_readback_texture(RHI_ASPECT_DEPTH) of a D32F_S8 texture
     * returns float depth (frame dumps and tests that compare depth); false
     * on D24S8, where that readback fails (returns false) */
    bool depthReadback;
    /* what backs RHI_FMT_D32F_S8: "D32S8", or "D24S8" (Vulkan fallback) */
    const char *depthStencilFormatName;
    /* Buffer<->texture copies: the row pitch and the buffer offset must be
     * multiples of these (D3D12: 256 and 512; Vulkan: 1 and 4).  rd_core
     * rounds the pitch up to copyRowPitchAlign from width * texel size. */
    uint32_t copyRowPitchAlign;
    uint32_t copyOffsetAlign;
    /* The Enhanced texture filter.  textureMips:
     * rhi_create_texture honours mipLevels > 1 for sampled RGBA8 textures,
     * rhi_cmd_copy_buffer_to_texture's mip selects the level (each level is
     * uploaded by the caller; no GPU mip generation), and samplers apply
     * RhiSamplerDesc.mip, minLod/maxLod and maxAnisotropy.  maxAnisotropy:
     * the largest RhiSamplerDesc.maxAnisotropy that takes effect (1 = none).
     * Both backends set them; a backend that leaves
     * textureMips false gets one level per texture from rd. */
    bool textureMips;
    float maxAnisotropy;
    /* Dynamic uniforms.  uniformAlign above is also the alignment of every
     * dynamic offset (Vulkan minUniformBufferOffsetAlignment, D3D12 256).
     * maxDynamicUniforms: the RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots one
     * pipeline's layouts may have together (Vulkan
     * maxDescriptorSetUniformBuffersDynamic, at least 8; D3D12 8, a budget
     * of the 64-DWORD root signature).  maxStorageRange: the largest range
     * of an RHI_BIND_STORAGE_BUFFER binding (Vulkan maxStorageBufferRange,
     * at least 2^27; D3D12 2^27 elements of 16 bytes). */
    uint32_t maxDynamicUniforms;
    uint64_t maxStorageRange;
    /* Texture packs: rhi_create_texture takes the RHI_FMT_BC* formats
     * (Vulkan textureCompressionBC with the formats' sampled and copy
     * features, every D3D12 device) and refuses them while this is false.
     * Their buffer-to-texture copies: rowPitch is the bytes of one row of
     * 4x4 blocks (ceil(w / 4) * 8 or 16, rounded up to copyRowPitchAlign),
     * srcOffset a multiple of the block bytes and of copyOffsetAlign, the
     * region in texels with x, y multiples of 4 and w, h either multiples
     * of 4 or reaching the level's edge (a level under 4 x 4 passes its
     * real size). */
    bool bcTextures;
} RhiLimits;

typedef struct RhiDeviceDesc {
    void *sdlWindow; /* SDL_Window*: the backend creates its surface/swapchain from it.
                      * NULL = headless (no swapchain; tests, tools/verify replay). */
    bool vsync;
    bool debugLayers;
    const char *appName;
} RhiDeviceDesc;

typedef enum RhiBackendKind {
    RHI_BACKEND_VULKAN = 0,
    RHI_BACKEND_D3D12 = 1,
    RHI_BACKEND_METAL = 2
} RhiBackendKind;

bool rhi_init(const RhiDeviceDesc *desc);
void rhi_shutdown(void);
RhiBackendKind rhi_backend(void);
const RhiLimits *rhi_limits(void);
const char *rhi_adapter_name(void);
/* True once the device is gone (Vulkan VK_ERROR_DEVICE_LOST; D3D12
 * DXGI_ERROR_DEVICE_REMOVED, _RESET or _HUNG).  The backend logged the
 * reason once; every later call is a no-op or fails, and the caller ends
 * the session (port/platform/window_host.c). */
bool rhi_device_lost(void);

/* Swapchain.  Resize is driven by rd_present from SDL window events. */
bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync);
RhiFormat rhi_swapchain_format(void);
/* The current swapchain's image size.  The backend rebuilds
 * the swapchain on its own (Vulkan: out of date, an Android suboptimal
 * present at another size, a lost or recreated surface; D3D12: a present
 * after the window's size changed) at the surface's size, which can differ
 * from the size the renderer last asked for; the renderer compares its
 * output size with this one at each acquire and follows it.  False (and
 * *w, *h left alone) when there is no swapchain (headless, the surface
 * released). */
bool rhi_swapchain_size(uint32_t *w, uint32_t *h);
/* The surface-size poll runs at every present again for a
 * while (as it does after a swapchain is made); called at the first game
 * frame, which can come long after the window opened (the disc import and
 * extraction on a first start), so the opening scene is covered.  A no-op
 * on backends that do not poll. */
void rhi_surface_poll_restart(void);
/* Acquire the next backbuffer image for this frame; returns id 0 when the
 * swapchain must be recreated. */
RhiTexture rhi_acquire_backbuffer(void);
/* The backbuffer's state after acquire is UNDEFINED; before rhi_present the
 * caller moves it to RHI_STATE_PRESENT. */
void rhi_present(void);
/* The window's surface (the Android lifecycle).
 * rhi_release_surface waits for the GPU, destroys the swapchain and the
 * surface and keeps the device and everything made on it: the app goes to
 * the background, where the system takes the window away.  Until a surface
 * is made again rhi_acquire_backbuffer returns id 0 and rhi_present does
 * nothing; rhi_swapchain_format keeps the last swapchain's format.
 * rhi_recreate_surface makes a surface on window (an SDL_Window*; NULL: the
 * one rhi_init was given) and a swapchain at the window's pixel size with
 * the last vsync setting; a surface still there is released first.  False
 * when the window has no native surface yet (the caller tries again later)
 * or the device is headless.  Vulkan also does this itself when an acquire
 * or a present returns VK_ERROR_SURFACE_LOST_KHR (that frame is skipped),
 * and rhi_resize_swapchain without a surface tries to make one.  D3D12: the
 * swapchain stays on its window; release does nothing, recreate returns
 * whether there is a swapchain. */
void rhi_release_surface(void);
bool rhi_recreate_surface(void *window);

/* Resources.  Create/destroy are not frame-safe: destroy defers internally
 * until the frames in flight that may reference the handle have retired. */
RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc);
void rhi_destroy_buffer(RhiBuffer b);
void *rhi_map_buffer(
    RhiBuffer
        b); /* UPLOAD and READBACK only; persistently mapped, returns the same pointer each time */
void rhi_unmap_buffer(RhiBuffer b);

RhiTexture rhi_create_texture(const RhiTextureDesc *desc);
void rhi_destroy_texture(RhiTexture t);
RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc);
void rhi_destroy_sampler(RhiSampler s);

RhiShader rhi_create_shader(const RhiShaderDesc *desc);
void rhi_destroy_shader(RhiShader s);
RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc);
void rhi_destroy_bind_group_layout(RhiBindGroupLayout l);
RhiBindGroup rhi_create_bind_group(
    const RhiBindGroupDesc *desc); /* transient: valid for the current frame only */
RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *desc);
void rhi_destroy_pipeline(RhiPipeline p);

/* ------------------------------------------------------------ commands
 * One command list per frame is enough for the game; rd_core may open a
 * second for texture uploads.  Lists are submitted in order.  Barriers are
 * needed only when a texture changes state: two writes in the same state
 * (copy after copy into one texture, render pass after render pass on one
 * target) are ordered by the backend. */
RhiCommandList rhi_begin_commands(void);
void rhi_end_commands(RhiCommandList cl);
void rhi_submit(RhiCommandList cl);
/* Block until the GPU has finished the frame submitted RHI_FRAMES_IN_FLIGHT
 * frames ago; rd_core calls it before reusing a ring region.  It is also the
 * frame boundary: it starts a new frame, and the command lists and transient
 * bind groups of the frame that used the same slot are recycled.  Call it
 * once per frame, before recording. */
void rhi_wait_frame(void);
/* The frame-in-flight slot (0 .. RHI_FRAMES_IN_FLIGHT - 1) of the frame
 * rhi_wait_frame last started: the slot whose previous use the GPU has
 * finished.  Per-frame resources outside the RHI (upload buffers, query
 * results) are keyed by it, not by a counter of their own. */
uint32_t rhi_frame_slot(void);
/* Full GPU idle, for shutdown, resize and verification readbacks. */
void rhi_wait_idle(void);

void rhi_cmd_barrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count);
void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass);
void rhi_cmd_end_render_pass(RhiCommandList cl);
void rhi_cmd_set_viewport(RhiCommandList cl, const RhiViewport *vp);
void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *rect);
void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p);
void rhi_cmd_set_bind_group(RhiCommandList cl, uint32_t group, RhiBindGroup bg);
/* Binds a group with the offsets of its layout's
 * RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots, one per such slot in ascending slot
 * order (count = their number), each a multiple of rhi_limits()->uniformAlign
 * and added to the binding's base offset.  rhi_cmd_set_bind_group is this call
 * with every offset 0.  Binding the group already bound with the same
 * offsets records nothing on Vulkan; D3D12 sets the group's root arguments
 * again at the next draw. */
void rhi_cmd_set_bind_group_offsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                    const uint32_t *offsets, uint32_t count);
void rhi_cmd_set_vertex_buffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset);
void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32);
void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref);
void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4]);
void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount);
void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount);

/* Copies, outside render passes.  Texture<->texture copies replace the
 * game's VRAM-to-VRAM moves (gif_MoveImage, ZFog's Z copy, queen barrier's
 * framebuffer grab).  Textures must be in COPY_SRC / COPY_DST state. */
/* Buffer copy: fills RHI_MEM_DEVICE buffers (meshes) from an UPLOAD buffer.
 * Buffers have no rhi_cmd_barrier; the backend makes the written range
 * visible to every later vertex, index, uniform, storage and copy read, and
 * the copy waits for every earlier read or copy of the
 * destination recorded or submitted before it, so a range the GPU may still
 * be reading for an earlier frame can be rewritten by a copy. */
void rhi_cmd_copy_buffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                         uint64_t dstOffset, uint64_t size);
void rhi_cmd_copy_buffer_to_texture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                    uint32_t rowPitch, RhiTexture dst, uint32_t mip,
                                    RhiRect region);
void rhi_cmd_copy_texture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                          int32_t dstX, int32_t dstY);
void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch);

/* Debug markers: no-ops without debug layers. */
void rhi_cmd_begin_label(RhiCommandList cl, const char *name);
void rhi_cmd_end_label(RhiCommandList cl);

/* -------------------------------------------------- verification helpers
 * Synchronous readback of a whole texture into caller memory as tightly
 * packed rows (RGBA8 or R32F for depth).  Slow; only used by the frame-dump
 * path and the headless replayer (tools/verify).  Mip 0 only; texels are
 * the format's raw bytes (BGRA order for the swapchain format).  The caller
 * puts the texture in RHI_STATE_COPY_SRC first (backends do not track
 * state); it stays there.  Work already submitted completes first. */
bool rhi_readback_texture(RhiTexture t, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch);

/* ------------------------------------------------------------- performance
 * Counters since rhi_init, cumulative: the caller takes differences
 * (rd_core's per-replay records).  D3D12: not implemented, every field
 * reads 0.  The
 * *Ns fields are CPU time blocked in the backend: fenceWaitNs on GPU
 * completion (rhi_wait_frame, rhi_wait_idle, readbacks), acquireNs in the
 * swapchain acquire, presentNs in the present call. */
typedef struct RhiStats {
    uint64_t buffersCreated, buffersDestroyed;
    uint64_t texturesCreated, texturesDestroyed;
    uint64_t memoryAllocs, memoryFrees; /* device memory allocations (VkDeviceMemory, heaps) */
    uint64_t bindGroups;                /* transient bind groups created */
    uint64_t pipelineBinds;  /* pipeline changes recorded (a repeat of the bound one is not) */
    uint64_t bindGroupBinds; /* bind group changes recorded (likewise) */
    uint64_t draws, renderPasses, barriers, copies;
    uint64_t submits, presents;
    uint64_t fenceWaits, fenceWaitNs;
    uint64_t waitIdles, readbacks;
    uint64_t acquireNs, presentNs;
    /* gauges, not counts: the device memory objects alive now and
     * the most ever, their bytes, and the device's limit on their number
     * (Vulkan; 0 elsewhere) */
    uint64_t memoryLive, memoryPeak, memoryLiveBytes, memoryPeakBytes, memoryLimit;
} RhiStats;

void rhi_get_stats(RhiStats *out);

/* GPU timestamps.  Each frame slot (rhi_wait_frame) has RHI_MAX_TIMESTAMPS
 * of them; rhi_cmd_write_timestamp(cl, i) records slot i when the GPU has
 * finished everything submitted or recorded before it (inside or outside a
 * render pass).  rhi_read_timestamps hands back the slot rhi_wait_frame last
 * recycled, i.e. the frame RHI_FRAMES_IN_FLIGHT frames ago, which has
 * completed, so reading never waits: ns[i] in nanoseconds from an arbitrary
 * origin, 0 for an index that frame did not write; returns 1 + the highest
 * index written (0 = none, or timestamps unsupported).
 * Vulkan: query pools.  D3D12: not implemented yet (unsupported: the calls
 * do nothing and read 0). */
#define RHI_MAX_TIMESTAMPS 32
bool rhi_timestamps_supported(void);
void rhi_cmd_write_timestamp(RhiCommandList cl, uint32_t index);
uint32_t rhi_read_timestamps(uint64_t *ns, uint32_t max);

/* Swapchain present mode with vsync on: mailbox (the newest
 * finished image is shown at each refresh, older ones are dropped; a present
 * never waits for the display) instead of FIFO.  Applies from the next
 * swapchain (re)creation; rhi_present_mailbox says whether the current one
 * uses it (false when the surface does not offer it: FIFO is kept).  D3D12:
 * not offered (false). */
void rhi_prefer_mailbox(bool on);
bool rhi_present_mailbox(void);
/* The current swapchain's present mode, for the logs: "fifo",
 * "mailbox", "immediate", "fifo_relaxed", or "none" without a swapchain.
 * Vulkan: the mode it was created with (vsync off prefers immediate, then
 * mailbox).  D3D12: "fifo" with vsync; without, "immediate" when DXGI
 * allows tearing, else "mailbox" (a flip-model present at sync interval 0
 * replaces the queued frame). */
const char *rhi_present_mode_name(void);

/* A pipeline cache kept across runs: Vulkan loads a VkPipelineCache
 * blob from path at rhi_init when its header names this device (header
 * version one, vendor and device ids, pipelineCacheUUID; anything else
 * starts empty), creates every pipeline through it, and writes it back at
 * rhi_shutdown (path.tmp, then renamed over path), so a later start compiles
 * nothing a driver without its own disk cache already compiled.  NULL or ""
 * (the default; the tests): no file.  Call before rhi_init; the path is
 * copied.  D3D12: ignored (its drivers keep their own shader cache). */
void rhi_set_pipeline_cache_path(const char *path);

/* The Vulkan loader rhi_init uses, as its
 * vkGetInstanceProcAddr: a graphics driver the Android build loaded itself
 * (port/platform/android/gpu_driver_android.c, through libadrenotools).
 * NULL (the default) uses SDL's, the system's loader.  Call before rhi_init;
 * kept across rhi_shutdown.  On Android the window's surface is then made
 * through the same function (vk_surface_android.c).  D3D12: ignored. */
void rhi_set_vulkan_loader(void *getInstanceProcAddr);

/* Programs that hook the graphics API from outside,
 * for the logs and the pacing; valid after rhi_init, NULL when none (and
 * before rhi_init).
 * rhi_injector_name: an effects program, "ReShade" or "vkBasalt".  Vulkan:
 * an instance layer of the loader (rhi_injector_from_layer_name) switched on
 * (rhi_layer_switched_on); ReShade's layer is implicit, loaded whenever it is
 * installed, and ReShade itself decides whether it draws in this program.
 * D3D12: a dxgi.dll or d3d12.dll loaded from outside the system directory
 * (ReShade's dxgi.dll beside the program), "ReShade".
 * rhi_overlay_name: an overlay layer, "Steam overlay" or "Mesa overlay"
 * (Vulkan only; D3D12: NULL). */
const char *rhi_injector_name(void);
const char *rhi_overlay_name(void);
/* The classifiers behind them (pure, any backend, tests): an instance layer
 * name to the injector or overlay it belongs to, NULL for any other layer:
 * VK_LAYER_reshade (ReShade), VK_LAYER_VKBASALT_post_processing (vkBasalt);
 * VK_LAYER_VALVE_steam_overlay_* (Steam overlay), VK_LAYER_MESA_overlay
 * (Mesa overlay). */
const char *rhi_injector_from_layer_name(const char *layer);
const char *rhi_overlay_from_layer_name(const char *layer);
/* The loader lists every installed layer (implicit or explicit, switched on
 * or not), so a listed layer is in this program only when the environment
 * leaves it on: vkBasalt (implicit) needs ENABLE_VKBASALT=1, its manifest's
 * enable_environment; the Mesa overlay (explicit) needs VK_INSTANCE_LAYERS
 * or VK_LOADER_LAYERS_ENABLE to name it; VK_LOADER_LAYERS_DISABLE naming a
 * layer switches it off, as does the layer's own disable variable set to
 * anything (its manifest's disable_environment: DISABLE_VK_LAYER_reshade_1,
 * DISABLE_VKBASALT, DISABLE_VK_LAYER_VALVE_steam_overlay_1).  The other layers above are implicit without an
 * enable variable: on.  env reads a variable (getenv; a table in tests). */
bool rhi_layer_switched_on(const char *layer, const char *(*env)(const char *name));

/* --------------------------------------------------- backend selection
 * rhi_create_backend selects the backend every call
 * above goes to: "vulkan" or "d3d12" (case-insensitive), NULL or "" for the
 * default.  The default is the ICO_RHI_BACKEND environment variable when it
 * names a linked backend, else the first linked one (Vulkan when it is
 * linked).  Call it before rhi_init; without a call the default is used.
 * Returns false, selection unchanged, for a name that is not linked into
 * this build or while a device is up (between rhi_init and rhi_shutdown).
 * rhi_backend_name lists the linked backends: index 0, 1, ... until NULL. */
bool rhi_create_backend(const char *name);
const char *rhi_backend_name(uint32_t index);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RHI_RHI_H */

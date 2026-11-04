/* vk_internal.h: shared state of the Vulkan backend of port/rhi/rhi.h. */
#ifndef PORT_RHI_VK_VK_INTERNAL_H
#define PORT_RHI_VK_VK_INTERNAL_H

#ifdef ICO_RHI_MOLTENVK
#define VK_ENABLE_BETA_EXTENSIONS 1
#include <vulkan/vulkan.h>
/* Directly linked functions exist; extension support is checked separately. */
#define VKR_FUNCTION_AVAILABLE(fn) true
#else
#include "volk.h"
#define VKR_FUNCTION_AVAILABLE(fn) ((fn) != NULL)
#endif
#include "../rhi.h"
#include "vk_enums.h"
#include <stdio.h>

/* ------------------------------------------------------------ handle pools
 * A handle id is (generation << VKR_GEN_SHIFT) | (index + 1).  The
 * generation is bumped when a slot is freed, so a stale handle fails the
 * lookup instead of aliasing a newer object. */
#define VKR_INDEX_BITS 20u
#define VKR_INDEX_MASK ((1u << VKR_INDEX_BITS) - 1u)
#define VKR_GEN_SHIFT VKR_INDEX_BITS
#define VKR_GEN_MASK 0xFFFu

typedef struct VkrPool {
    const char *name;
    uint32_t cap;
    uint32_t elemSize;
    uint8_t *data;
    uint16_t *gen;
    uint8_t *live;
    uint32_t *freeList;
    uint32_t freeCount;
    /* first never-used index: a freed slot is taken again before it moves,
     * so it is also the most slots alive at once */
    uint32_t next;
    uint64_t full; /* allocations refused because every slot was taken */
} VkrPool;

bool vkr_pool_init(VkrPool *p, const char *name, uint32_t cap, uint32_t elemSize);
void vkr_pool_free(VkrPool *p);
/* Allocates a zeroed element; returns its id (0 when full: the line
 * "<name> pool full (cap); N refused so far" the first 8 times, then every
 * 100th). */
uint32_t vkr_pool_alloc(VkrPool *p, void **out);
void *vkr_pool_get(const VkrPool *p, uint32_t id);
void vkr_pool_release(VkrPool *p, uint32_t id);

/* --------------------------------------------------------------- objects */
typedef struct VkrBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize memSize; /* the allocation's size (vkr_allocate) */
    uint64_t size;
    RhiMemory kind;
    void *mapped;
    bool coherent;
    /* a device buffer made for vertex, index, uniform or storage reads
     * (rhi.h rhi_cmd_copy_texture_to_buffer's ordering) */
    bool shaderRead;
    /* hazard tracking (vk_cmd.c, "Hazards"): a copy's write or read not yet
     * behind a barrier, valid while hzEpoch is the recording list's */
    uint64_t hzEpoch;
    bool hzXferWrite, hzXferRead;
} VkrBuffer;

typedef struct VkrTexture {
    VkImage image;
    VkDeviceMemory memory; /* VK_NULL_HANDLE for swapchain images */
    VkDeviceSize memSize;  /* the allocation's size (vkr_allocate) */
    VkImageView view;      /* all aspects: attachments, colour sampling */
    VkImageView depthView; /* depth aspect only, depth-stencil formats that are sampled */
    /* stencil aspect only, the same textures single-sampled (RHI_ASPECT_STENCIL) */
    VkImageView stencilView;
    VkImageView uintView; /* RGBA8_UINT, an RHI_TEX_UINT_VIEW texture's (RHI_ASPECT_UINT) */
    VkFormat format;
    RhiFormat rhiFormat;
    VkImageAspectFlags aspects;
    uint32_t width, height, mips;
    uint32_t samples; /* 1, or the multisampled count (RhiTextureDesc.samples) */
    bool swapchain;
    /* hazard tracking (vk_cmd.c, "Hazards"): what touched the image since
     * its last barrier, valid while hzEpoch is the recording list's: the
     * mips a copy wrote, and an attachment write or read */
    uint64_t hzEpoch;
    uint32_t hzXferMips;
    uint8_t hzAttach; /* VKR_HZ_ATTACH_* */
} VkrTexture;

#define VKR_HZ_ATTACH_WRITE 1u
#define VKR_HZ_ATTACH_READ 2u

typedef struct VkrShader {
    VkShaderModule module;
    RhiShaderStage stage;
    char entry[64];
} VkrShader;

typedef struct VkrLayout {
    VkDescriptorSetLayout layout;
    uint32_t slotCount;
    RhiBindSlot slots[16];
    uint32_t dynamicCount; /* RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots */
} VkrLayout;

/* The dynamic depth and blend state (rhi.h RhiPipelineDesc.dynamic; vk_cmd.c
 * vkr_flush_dynamic): the values a draw needs, as the Vulkan commands take
 * them.  Compared with memcmp, so always made from a zeroed value. */
typedef struct VkrDepthDyn {
    VkBool32 test, write;
    VkCompareOp compare;
} VkrDepthDyn;

typedef struct VkrBlendDyn {
    VkBool32 enable;
    VkColorBlendEquationEXT eq;
    VkColorComponentFlags mask;
} VkrBlendDyn;

typedef struct VkrPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    uint32_t layoutCount;
    /* the RhiBindGroupLayout id of each set of the layout: a pipeline
     * change keeps the descriptor sets bound below the first set whose
     * layout differs (Vulkan's pipeline layout compatibility: identical
     * set layouts, no push constants; vk_cmd.c vkr_groups_disturbed) */
    uint32_t setLayoutIds[RHI_MAX_BIND_SLOTS];
    uint8_t dstRead; /* RhiDstRead (vk_cmd.c vkr_dst_order) */
    /* the RHI_DYN_* states the pipeline leaves dynamic (only where the
     * device has them: g_vkr.dynDepth, dynBlend), their values in the
     * description (a draw's until rhi_cmd_set_depth_state /
     * rhi_cmd_set_blend_state after the bind), and whether colour target 0
     * is an integer format (no blending there) */
    uint8_t dynamic;
    bool intTarget;
    VkrDepthDyn depthDefault;
    VkrBlendDyn blendDefault;
} VkrPipeline;

/* vk_pipeline.c: an RHI blend state as Vulkan's attachment state (blending
 * off on an integer target), and that state as the dynamic blend values */
VkPipelineColorBlendAttachmentState vkr_blend_state(const RhiBlendState *b, bool intTarget);
void vkr_blend_dyn(const VkPipelineColorBlendAttachmentState *a, VkrBlendDyn *out);

/* --------------------------------------------------------- per frame slot */
typedef enum VkrGarbageKind {
    VKR_GARBAGE_BUFFER,
    VKR_GARBAGE_IMAGE,
    VKR_GARBAGE_VIEW,
    VKR_GARBAGE_MEMORY,
    VKR_GARBAGE_SAMPLER,
    VKR_GARBAGE_PIPELINE,
    VKR_GARBAGE_PIPELINE_LAYOUT,
    VKR_GARBAGE_SET_LAYOUT,
    VKR_GARBAGE_SHADER,
    /* vk_renderpass.c: a cached framebuffer whose view is destroyed (before
     * the views: deferred in that order) or that the full cache replaced,
     * and a render pass made for one use when its cache is full */
    VKR_GARBAGE_FRAMEBUFFER,
    VKR_GARBAGE_RENDERPASS
} VkrGarbageKind;

typedef struct VkrGarbage {
    VkrGarbageKind kind;
    uint64_t handle; /* any non-dispatchable Vulkan handle */
    uint64_t size;   /* VKR_GARBAGE_MEMORY: the allocation's size (vkr_defer_memory) */
    uint32_t slot;   /* overflow entries only: the frame slot that deferred it */
} VkrGarbage;

/* Deferred destroys that found their frame's garbage list unable to grow
 * (out of memory): kept here, per entry with its frame slot, and destroyed
 * with that slot's own garbage (vkr_destroy_garbage). */
#define VKR_GARBAGE_OVERFLOW 256

#define VKR_MAX_CMD_LISTS 8
/* submissions kept for the loss report (VkrState.submits) */
#define VKR_SUBMIT_HISTORY 8
/* image barriers a command list holds back before issuing them together
 * (vk_cmd.c, vkr_flush_barriers) */
#define VKR_PENDING_BARRIERS 8
#define VKR_DESC_POOLS_MAX 16
/* swapchain images: the most a surface's swapchain may have (vk_swapchain.c) */
#define VKR_MAX_SWAP_IMAGES 16u

/* The submission sync (vk_sync.c): what tells the CPU a submission is done.
 * Every submission gets the next number of the port's counter
 * (VkrState.timelineValue); VKR_SYNC_TIMELINE signals that number on one
 * timeline semaphore (Vulkan 1.2, or VK_KHR_timeline_semaphore), and
 * VKR_SYNC_FENCES, for a device without it, signals a fence of a ring of
 * VKR_SYNC_FENCE_RING, the fence of number n at slot n % VKR_SYNC_FENCE_RING
 * (one queue signals in submission order, so the newest fence found
 * signalled is exactly the newest number finished). */
typedef enum VkrSyncKind { VKR_SYNC_TIMELINE, VKR_SYNC_FENCES } VkrSyncKind;

#define VKR_SYNC_FENCE_RING 8u

/* The render passes of a device without dynamic rendering (core 1.3 or
 * VK_KHR_dynamic_rendering): classic VkRenderPass objects
 * (vk_renderpass.c, g_vkr.legacyPasses), and ICO_VK_FAKE_API=1.1 hides
 * VK_KHR_dynamic_rendering so its runs take them.  Set to 0, such a device
 * is unsuitable again (vkr_rate_device, "no dynamic rendering") and the
 * 1.1 profile keeps the extension. */
#define VKR_LEGACY_PASSES 1

/* One vkQueueSubmit through vkr_sync_submit: the command buffers (possibly
 * none), the binary semaphore to wait on first (the swapchain acquire,
 * VK_NULL_HANDLE: none) at waitStage, afterAll: the batch is ordered after
 * every earlier submission (the empty present batch: a wait on the
 * timeline's previous number; the fence path has no such wait and needs
 * none, a semaphore signal already covers every earlier submission on the
 * queue), and the binary semaphore to signal (the image's renderDone,
 * VK_NULL_HANDLE: none). */
typedef struct VkrSubmitDesc {
    const VkCommandBuffer *cbs;
    uint32_t cbCount;
    VkSemaphore wait;
    VkPipelineStageFlags waitStage;
    bool afterAll;
    VkSemaphore signal;
} VkrSubmitDesc;

typedef struct VkrCmdList {
    VkCommandBuffer cb;
    bool recording;
    bool submitted;
    /* the frame slot whose pool the list came from (rhi_begin_commands):
     * the end and the submit find the slot's own acquire, fence value and
     * query pools through it, not through the current frame index, which
     * the recording thread may have moved on ("Frame slots" in vk_cmd.c) */
    uint8_t slot;
    /* draw-time state */
    VkrPipeline *pipeline;
    VkDescriptorSet groups[RHI_MAX_BIND_SLOTS];
    /* each bound group's dynamic offsets, in binding order */
    uint32_t dynCount[RHI_MAX_BIND_SLOTS];
    uint32_t offsets[RHI_MAX_BIND_SLOTS][RHI_MAX_DYNAMIC_OFFSETS];
    uint32_t groupDirty; /* bit per group */
    bool inPass;
    /* the open pass's render area (vk_cmd.c vkr_pass_area_fits, and
     * RhiRenderPassDesc.area inside it): offset areaX, areaY, extent areaW
     * x areaH; every scissor set inside the pass is cut to it */
    uint32_t areaX, areaY, areaW, areaH;
    /* a draw, and a RHI_DST_READ_STORAGE draw, recorded in the open
     * render pass (vk_cmd.c vkr_dst_order) */
    bool drawnInPass, storageDrawn;
    /* the list's hazard-tracking epoch (g_vkr.hzEpoch when it began);
     * globalOrder: a global barrier before every pass and copy instead
     * (ICO_VK_GLOBAL_BARRIERS=1, or lists recorded interleaved) */
    uint64_t epoch;
    bool globalOrder;
    /* image barriers recorded by rhi_cmd_barrier and not issued yet, with
     * their stage masks OR-ed: they go out in one vkCmdPipelineBarrier
     * before the list's next command (vk_cmd.c, vkr_flush_barriers) */
    VkImageMemoryBarrier pendImg[VKR_PENDING_BARRIERS];
    uint32_t pendCount;
    VkPipelineStageFlags pendSrc, pendDst;
    /* the accesses a copy into a buffer shaders read must be made visible
     * to (rhi.h rhi_cmd_copy_texture_to_buffer), 0 for none: a memory
     * barrier from the transfer writes, issued with the pending image
     * barriers */
    VkAccessFlags pendMemDst;
    /* the dynamic depth and blend state (vk_cmd.c vkr_flush_dynamic):
     * dynValid the RHI_DYN_* groups whose values in dynHave the command
     * buffer holds (a bind of a pipeline with the group static clears its
     * bit: that pipeline's own values replaced them), dynWant what the next
     * draw needs (the bound pipeline's defaults, then the sets), dynSet the
     * groups set since the bind */
    uint8_t dynValid, dynSet;
    VkrDepthDyn depthWant, depthHave;
    VkrBlendDyn blendWant, blendHave;
    /* the swapchain image the list moves to RHI_STATE_PRESENT
     * (VK_NULL_HANDLE: none): its submit signals the present semaphore
     * (rhi_submit) */
    VkImage presentImage;
    /* the occlusion query open in the list (rhi_cmd_begin_occlusion),
     * its index + 1; 0 for none */
    uint32_t occOpen;
    /* The bind cache (vk_cmd.c; docs/port/RHI.md "The bind cache"): the
     * bound pipeline's id (pipeline above is its pool slot, which a
     * destroyed pipeline's successor takes over), and what the set calls
     * last recorded, valid for the VKR_KNOW_* bits of known (the vertex
     * buffers per binding in vbKnown): a set equal to it records nothing.
     * Zero at the slot's recycle; the viewport and scissor are forgotten
     * at each pass's start.  dynFresh: the RHI_DYN_* groups set since the
     * last draw (RhiStats.stateSetsSkipped counts their commands the draw
     * found in force) */
    uint32_t pipelineId;
    uint32_t known;
    VkViewport vp;
    VkRect2D sc;
    uint32_t vbId[RHI_MAX_VERTEX_ATTRS];
    uint64_t vbOff[RHI_MAX_VERTEX_ATTRS];
    uint32_t vbKnown;
    uint32_t ibId;
    uint64_t ibOff;
    bool ib32;
    uint8_t stencilRef;
    float blendConst[4];
    uint8_t dynFresh;
} VkrCmdList;

/* A frame slot's swapchain image (vk_swapchain.c): acquired, not yet
 * presented; waitPending: its acquire semaphore (the slot's acquireSem)
 * not yet waited on by a submit of the slot; signalled: renderDone[image]
 * is signalled by the submit of the slot's list that moved the image to
 * PRESENT (rhi_submit), so rhi_present submits nothing before presenting;
 * seq: the acquire's number (VkrState.acqSeq), the present takes the
 * oldest acquired.  Written under the swapchain's lock by the acquire and
 * the present, and by the submit of the slot's own list (signalled,
 * waitPending), which the acquire's thread handed over after the acquire;
 * cleared for every slot where the swapchain is made again or released
 * (vkr_acq_reset_all), the device idle and the submit thread with it. */
typedef struct VkrAcquire {
    bool acquired;
    bool waitPending;
    bool signalled;
    uint32_t image;
    uint64_t seq;
} VkrAcquire;

typedef struct VkrFrame {
    uint64_t waitValue; /* timeline value of the frame's last submit */
    /* the frame index the slot was last recycled at, masked as a command
     * list handle's tag (VKR_GEN_MASK): vkr_get_cmd checks a list's tag
     * against its own slot's */
    uint32_t gen;
    /* the slot's swapchain image (VkrAcquire) */
    VkrAcquire acq;
    /* rhi_set_submit_tag's numbers for the slot's submits (the loss
     * report's and rhi_gpu_progress's names), set by the recording thread
     * after the slot's recycle and read by the slot's submits */
    uint32_t tagReplay, tagFrame;
    VkCommandPool cmdPool;
    VkrCmdList lists[VKR_MAX_CMD_LISTS];
    uint32_t listCount;
    VkDescriptorPool descPools[VKR_DESC_POOLS_MAX];
    uint32_t descPoolCount; /* pools created */
    uint32_t descPoolCur;   /* pool being allocated from */
    VkDescriptorSet *sets;  /* transient bind groups of this frame */
    uint8_t *setDynamic;    /* per set: its layout's dynamic slots */
    uint32_t setCount, setCap;
    VkrGarbage *garbage;
    uint32_t garbageCount, garbageCap;
    VkSemaphore acquireSem; /* swapchain image acquire */
    /* the slot's GPU timestamps (rhi_cmd_write_timestamp) */
    VkQueryPool queryPool;
    uint32_t tsWritten[RHI_TIMESTAMP_WORDS]; /* the indices written since the slot was recycled */
    bool tsReset; /* the pool's reset is recorded in this slot's first list */
    /* the slot's occlusion queries (rhi_cmd_begin_occlusion): the pool made
     * at the first begin of the run, the indices begun since the slot was
     * recycled, and whether the pool is reset for this use of the slot */
    VkQueryPool occPool;
    uint32_t occWritten[RHI_OCCLUSION_WORDS];
    bool occReset;
} VkrFrame;

/* A render pass as both paths take it (vk_cmd.c rhi_cmd_begin_render_pass
 * fills it, vk_renderpass.c vkr_pass_begin records it): per attachment its
 * texture, view, format and sample count, the layout it is in for the pass
 * (the port's state layout, vkr_state_layout: the hazard barriers before the
 * pass read it, and a VkRenderPass makes no transition), the load and store
 * ops (a depth-stencil format's stencil ops in stencilLoad/stencilStore; a
 * colour target's are DONT_CARE) and the clear value; hasStencil: the depth
 * format has a stencil aspect; readOnlyDepth as asked; the render area
 * (vkr_pass_area_fits, then RhiRenderPassDesc.area inside it: offset
 * areaX, areaY, extent areaW x areaH) and the framebuffer's size (the
 * smallest attachment's, the pass's own size without one). */
typedef struct VkrPassAttach {
    VkrTexture *tex;
    VkImageView view;
    VkFormat format;
    VkSampleCountFlagBits samples;
    VkImageLayout layout;
    VkAttachmentLoadOp load, stencilLoad;
    VkAttachmentStoreOp store, stencilStore;
    VkClearValue clear;
} VkrPassAttach;

typedef struct VkrPassDesc {
    uint32_t colorCount;
    VkrPassAttach color[RHI_MAX_COLOR_TARGETS];
    bool hasDepth, hasStencil, readOnlyDepth;
    VkrPassAttach depth;
    uint32_t areaX, areaY, areaW, areaH;
    uint32_t fbW, fbH;
} VkrPassDesc;

/* -------------------------------------------------------------- device */
typedef struct VkrState {
    bool initialised;
    RhiLimits limits;
    char adapterName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    bool debugUtils;
    VkPhysicalDevice phys;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties memProps;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue;
    uint32_t apiVersion;
    PFN_vkCmdBeginRendering cmdBeginRendering;
    PFN_vkCmdEndRendering cmdEndRendering;
    bool anisotropy;
    /* the Vulkan format behind RHI_FMT_D32F_S8 (D32_SFLOAT_S8_UINT, or
     * D24_UNORM_S8_UINT without it or under ICO_VK_FAKE_D24S8) */
    VkFormat dsFormat;
    bool d32fAttachment; /* D32_SFLOAT is a depth attachment (RhiLimits.depth32Attachment) */
    bool dsSampled;      /* dsFormat has the sampled-image feature (RhiLimits.depthSampled) */
    bool dualSrcBlend;   /* the feature enabled (RhiLimits.dualSourceBlend) */
    /* texture packs: textureCompressionBC enabled and the four BC formats
     * sampleable and copyable (RhiLimits.bcTextures) */
    bool bc;
    /* fullDrawIndexUint32 offered (not enabled: the index values stay below
     * 2^28; ICO_VK_FAKE_LIMITS=mali reports it absent) */
    bool fullDrawIndex;
    /* multisampling (RhiLimits.msaaCounts, sampleRateShading): the counts
     * every multisampled format takes, and the feature enabled */
    uint32_t msaaCounts;
    bool sampleRate;
    /* the destination read (RhiLimits.fragmentInterlock,
     * rasterOrderAttachment): the features enabled */
    bool interlock;
    bool rasterOrder;
    /* the attachment read the device has but is not offered on (Qualcomm's
     * own driver, rhi_driver_is_qualcomm_proprietary; ICO_VK_FAKE_ADRENO=1
     * takes that branch, ICO_VK_ATTACHMENT_READ=1 offers it anyway): why,
     * for RhiLimits.rasterOrderOff; "" when it is not denied */
    char rasterOrderOff[96];
    /* VkPhysicalDeviceDriverProperties.driverID and the driver's version as
     * its vendor writes it (vkr_driver_text) */
    uint32_t driverID;
    /* the device is MoltenVK (rhi_driver_is_moltenvk), or ICO_VK_FAKE_LIMITS=
     * moltenvk says so: RhiLimits.moltenvk; fakeMoltenvk is that profile */
    bool moltenvk;
    bool fakeMoltenvk;
    char driverVersion[32];
    /* driverProperties.driverName and the Mesa version of its driverInfo
     * (vkr_driver_text), for RhiLimits.driverName and .mesa */
    char driverName[VK_MAX_DRIVER_NAME_SIZE];
    RhiMesaVersion mesa;
    /* the driver's own "Compiler Version: ..." line from its driverInfo
     * (Qualcomm's Adreno driver writes one), "" when it has none, for
     * RhiLimits.compilerVersion (vkr_driver_text) */
    char compilerVersion[96];
    /* why the VU fallback's program entries are the plain ones on this
     * device (vkr_vu_plain_only's table: "Adreno 740, issue 107"), "" when
     * they are not, for RhiLimits.vuFallbackPlain */
    char vuFallbackPlain[96];
    /* why this device gets the careful start-up set (the same table's
     * careful column: "Adreno 740, issues 93 and 107"), "" otherwise, for
     * RhiLimits.carefulPipelines */
    char carefulPipelines[96];
    /* the dynamic state (vk_device.c vkr_create_device; RhiLimits.dynamicDepth,
     * dynamicBlend): dynDepth the depth test, write and compare (Vulkan 1.3,
     * or VK_EXT_extended_dynamic_state's extendedDynamicState), dynBlend the
     * blend enable, equation and write mask (VK_EXT_extended_dynamic_state3's
     * extendedDynamicState3ColorBlendEnable, ...ColorBlendEquation,
     * ...ColorWriteMask), each with its commands found
     * (vkGetDeviceProcAddr: the core names on 1.3, the EXT ones otherwise) */
    bool dynDepth, dynBlend;
    PFN_vkCmdSetDepthTestEnable cmdSetDepthTestEnable;
    PFN_vkCmdSetDepthWriteEnable cmdSetDepthWriteEnable;
    PFN_vkCmdSetDepthCompareOp cmdSetDepthCompareOp;
    PFN_vkCmdSetColorBlendEnableEXT cmdSetColorBlendEnable;
    PFN_vkCmdSetColorBlendEquationEXT cmdSetColorBlendEquation;
    PFN_vkCmdSetColorWriteMaskEXT cmdSetColorWriteMask;
    /* VK_KHR_dynamic_rendering_local_read enabled (the attachment read, and
     * barriers inside a render pass); validationOn: the Khronos validation
     * layer is loaded; storageBarriers: a by-region barrier before a
     * destination-read draw after other draws of its pass (vk_cmd.c
     * vkr_dst_order) */
    bool localRead;
    bool validationOn;
    bool storageBarriers;
    uint32_t validationErrors;
    bool deviceLost; /* a call returned VK_ERROR_DEVICE_LOST (rhi_device_lost) */

    /* GPU loss diagnostics (docs/port/RHI.md, "Device loss"; vk_device.c
     * vkr_lost_report, printed once at the first VK_ERROR_DEVICE_LOST):
     *   robust       robustBufferAccess (+ VK_EXT_robustness2's
     *                robustImageAccess2 and nullDescriptor when offered),
     *                ICO_GPU_ROBUST=1 (ico-pc.ini gpu_robust, or the loss
     *                order's robust step), off by default everywhere;
     *   deviceFault  VK_EXT_device_fault enabled (whenever offered):
     *                vkGetDeviceFaultInfoEXT's addresses and description;
     *   checkpoints  VK_NV_device_diagnostic_checkpoints enabled,
     *                ICO_GPU_CHECKPOINTS=1 (ico-pc.ini gpu_checkpoints):
     *                rhi_cmd_checkpoint's markers, read back on a loss;
     *   addressReport VK_EXT_device_address_binding_report under the same
     *                switch: the GPU address of every buffer and image
     *                (vk_resource.c, the allocation table), so a faulting
     *                address names its resource. */
    bool robust, robust2, deviceFault, checkpoints, addressReport;
    PFN_vkGetDeviceFaultInfoEXT getFaultInfo;
    PFN_vkCmdSetCheckpointNV cmdSetCheckpoint;
    PFN_vkGetQueueCheckpointDataNV getCheckpointData;
    VkDebugUtilsMessengerEXT bindMessenger; /* the address binding report's */
    bool lostReported;
    uint64_t lostNs; /* vkr_now_ns at the first VK_ERROR_DEVICE_LOST */

    /* the last submissions (vkr_submit_batch, rhi_readback_texture): the
     * timeline value each signals, the rhi_set_submit_tag in force, its
     * command buffers and whether it signalled a present */
    struct {
        uint64_t value;
        uint32_t replay, frame;
        uint8_t lists, present;
    } submits[VKR_SUBMIT_HISTORY];

    uint32_t submitCount; /* ever; the newest is submits[(submitCount - 1) % N] */
    /* the timeline value of the wait that failed, 0: none (vkr_wait_value) */
    uint64_t lostWaitValue;
    /* rhi_gpu_progress (rhi.h; read from any thread): the newest
     * submission's value, when it was made (vkr_now_ns) and its tag
     * (vkr_note_submit), the newest value seen finished, and the value a
     * wait in progress blocks on with its start (vkr_wait_value,
     * rhi_wait_idle); stored with __atomic by the submitting thread */
    uint64_t progSubmitted, progSubmitNs, progFinished, progWaitingFor, progWaitSinceNs;
    uint32_t progReplay, progFrame;
    /* render passes whose area exceeded an attachment (vk_cmd.c
     * vkr_pass_area_fits: logged once, clamped; vkr_test_pass_area_count) */
    uint32_t passAreaBad;

    /* the submission sync (vk_sync.c, VkrSyncKind): timelineValue is the
     * port's counter, the number of the newest submission (one per submit,
     * whatever the kind: VkrFrame.waitValue, submits[], prog*,
     * lostWaitValue count in it); timeline the semaphore that signals it
     * (VKR_SYNC_TIMELINE) with its wait and counter read (the core entry
     * points, the KHR ones below Vulkan 1.2); fences the ring
     * (VKR_SYNC_FENCES), fenceValue the number each slot's fence signals (0:
     * none pending), fenceFinished the newest number seen finished,
     * fenceRecycleWaits the submits that had to wait for their slot's
     * earlier number (logged the first time) */
    VkrSyncKind syncKind;
    VkSemaphore timeline;
    uint64_t timelineValue;
    PFN_vkWaitSemaphores waitSemaphores;
    PFN_vkGetSemaphoreCounterValue getSemaphoreCounter;
    VkFence fences[VKR_SYNC_FENCE_RING];
    uint64_t fenceValue[VKR_SYNC_FENCE_RING];
    uint64_t fenceFinished;
    uint32_t fenceRecycleWaits;
    /* ICO_VK_FAKE_API (vk_device.c vkr_create_instance): the profile in
     * force ("1.0", "1.1", "1.1ext"; NULL: none), the 1.1 ones holding the
     * instance and the device to Vulkan 1.1, "1.0" to Vulkan 1.0 (issue
     * 158); fakeHideExts: the 1.0 and 1.1 profiles, whose device also lacks
     * the extensions a stock Vulkan 1.1 driver has not
     * (vkr_device_has_ext), so it has no timeline semaphores
     * (fakeNoTimeline, VKR_SYNC_FENCES); fakeNoProps2: "1.0bare", the
     * "1.0" profile (fakeApi "1.0") on a loader without
     * VK_KHR_get_physical_device_properties2 (review finding 5: the
     * instance goes on without it, the device without the extended queries) */
    const char *fakeApi;
    bool fakeHideExts;
    bool fakeNoTimeline;
    bool fakeNoProps2;
    /* the paths the device's version and extensions give (vkr_create_device;
     * driverProps vkr_driver_text): legacyPasses, render passes as VkRenderPass
     * objects (no dynamic rendering: vk_renderpass.c; cmdBeginRendering and
     * cmdEndRendering stay NULL);
     * driverProps, VkPhysicalDeviceDriverProperties queried (Vulkan 1.2 or
     * VK_KHR_driver_properties; without it driverID is 0 and the driver
     * unnamed); formatList, VkImageFormatListCreateInfo chained for the UINT
     * views (Vulkan 1.2 or VK_KHR_image_format_list); storeOpNone,
     * VK_ATTACHMENT_STORE_OP_NONE taken (Vulkan 1.3, or below it on a
     * Vulkan 1.1 device or one without dynamic rendering
     * VK_KHR_load_store_op_none, VK_EXT_load_store_op_none or
     * VK_QCOM_render_pass_store_ops, whose NONE_QCOM is the same value);
     * viewUsage, VkImageViewUsageCreateInfo chained on the UINT views
     * (Vulkan 1.1, or VK_KHR_maintenance2 on a Vulkan 1.0 device: issue
     * 158; without it the view keeps the image's usages) */
    bool legacyPasses;
    bool driverProps;
    bool formatList;
    bool storeOpNone;
    bool viewUsage;

    /* frames */
    VkrFrame frames[RHI_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;

    /* objects */
    VkrPool buffers, textures, samplers, shaders, layouts, pipelines;

    /* swapchain (absent when headless) */
    void *window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    /* the swapchain a recreation retired, kept until the first present on
     * the new one succeeds (destroying it at once took the last picture off
     * the screen for the length of the rebuild: a black flash at every
     * frame rate change); vkr_test_retired_swapchain */
    VkSwapchainKHR retired;
    VkFormat swapFormat;
    RhiFormat swapRhiFormat;
    VkColorSpaceKHR swapColorSpace;
    uint32_t swapWidth, swapHeight;
    /* the quarter turns clockwise the images are made in (rhi_swapchain_
     * rotation; vkr_prerotate_turns): 0 identity, else the surface's
     * transform is the preTransform and the images are swapWidth x
     * swapHeight turned (swapped at 1 and 3); swapWidth x swapHeight stay
     * the window's own orientation, the size the renderer draws at */
    uint32_t swapTurns;
    bool vsync;
    uint32_t swapImageCount;
    uint32_t swapTextures[VKR_MAX_SWAP_IMAGES];  /* texture ids */
    VkSemaphore renderDone[VKR_MAX_SWAP_IMAGES]; /* per image */
    /* the surface's minImageCount the swapchain was made with: a second
     * image may be acquired before the first is presented while
     * swapImageCount - swapMinImages is at least 1 (the spec's limit for an
     * acquire without a timeout; vk_swapchain.c vkr_acquire_ahead_ok) */
    uint32_t swapMinImages;
    /* the acquires' numbers (VkrAcquire.seq), under the swapchain's lock */
    uint64_t acqSeq;
    /* a present on the submit thread does not make the swapchain or the
     * surface again (the recording thread may run beside it,
     * rhi_slot_guard_worker): what it found is kept here, under the
     * swapchain's lock, and done at the next acquire, with the submit
     * thread idle (vk_swapchain.c vkr_take_deferred).  VKR_DEFER_* bits;
     * deferW/H and deferTurns the size and turn the poll found
     * (VKR_DEFER_POLL) */
    uint32_t deferred;
    uint32_t deferW, deferH, deferTurns;
    uint64_t deferredCount; /* ever, for the line */

    /* one-shot command pool for readback */
    VkCommandPool oneShotPool;

    /* every pipeline is created through it (vkr_pipeline_cache_init) */
    VkPipelineCache pipelineCache;

    /* counters (rhi_get_stats), timestamps, the mailbox option.  stats is
     * written by the recording thread alone (and by any thread while the
     * submit thread is idle); the counters a list's end, a submit and a
     * present add go into late with __atomic adds, from whichever thread
     * makes them, and rhi_get_stats adds them in (vk_device.c): the submit
     * thread's end, submit and present run beside the next picture's
     * recording (RhiLimits.slotOverlap) */
    RhiStats stats;

    struct {
        uint64_t barriers, submits, presents, presentNs;
    } late;

    /* RhiLimits.slotOverlap's answer and why not, for the device line */
    bool slotOverlap;
    bool timestamps;        /* the queue writes timestamps */
    float timestampPeriod;  /* ns per tick */
    uint64_t timestampMask; /* the valid bits */
    uint64_t tsResult[RHI_MAX_TIMESTAMPS];
    uint32_t tsCount; /* of the slot rhi_wait_frame recycled last */
    /* occlusion queries (rhi_cmd_begin_occlusion): the pools exist (made at
     * the first begin), the device counts precisely (occlusionQueryPrecise,
     * enabled), and the counts of the slot rhi_wait_frame recycled last
     * (RHI_MAX_OCCLUSION, allocated with the pools; RHI_OCCLUSION_NONE for
     * an index not written) with 1 + the highest index written */
    bool occUsed;
    bool occBroken; /* the pools or the array could not be made: no more tries this run */
    bool occPrecise;
    uint64_t *occResult;
    uint32_t occCount;
    bool mailbox; /* the swapchain presents in mailbox mode (rhi_prefer_mailbox) */

    /* the swapchain's present mode (rhi_present_mode_name) */
    VkPresentModeKHR presentMode;

    /* the display's timing of presents (rhi_present_timing): present ids
     * and waits enabled, the last id given to a present (ids keep rising
     * across swapchains), vkWaitForPresentKHR */
    bool presentWait;
    bool presentWaitFake; /* vkr_test_fake_present_wait: no ids chained */
    /* VK_PRESENT_MODE_FIFO_LATEST_READY (VK_EXT_present_mode_fifo_latest_ready)
     * enabled on the device, asked for with ICO_VK_FIFO_LATEST_READY=1:
     * vkr_choose_present_mode takes it where FIFO would be taken */
    bool fifoLatestReady;
    /* Windows: VK_KHR_get_surface_capabilities2 on the instance and
     * VK_EXT_full_screen_exclusive on the device, both offered: the
     * swapchain and its surface queries carry
     * VkSurfaceFullScreenExclusiveInfoEXT (rhi_set_exclusive_fullscreen).
     * presentModes2 is vkGetPhysicalDeviceSurfacePresentModes2EXT, loaded
     * here (volk declares it only with VK_USE_PLATFORM_WIN32_KHR, which
     * this build does not define); false and NULL elsewhere */
    bool surfaceCaps2;
    bool fullScreenExclusive;
    PFN_vkVoidFunction presentModes2;
    uint64_t presentId;
    PFN_vkWaitForPresentKHR waitForPresent;

    /* hazard tracking (vk_cmd.c, "Hazards") */
    bool globalBarriers; /* ICO_VK_GLOBAL_BARRIERS=1: a global barrier before every pass and copy */
    /* ICO_VK_DEPTH_ALWAYS=1: every depth test compares ALWAYS (the
     * occlusion measurement's run without the depth: what draws nothing
     * whatever the depth says; the picture is not the game's) */
    bool depthAlways;
    uint64_t hzEpoch; /* the last command list's epoch (one per rhi_begin_commands) */

    VkrGarbage overflow[VKR_GARBAGE_OVERFLOW]; /* see VKR_GARBAGE_OVERFLOW */
    uint32_t overflowCount;

    /* The device memory objects alive and their bytes, with the peaks, and
     * the samplers alive.  Every buffer and texture has its own allocation
     * (vk_resource.c), and maxMemoryAllocationCount is 4096 on many phone
     * GPUs (desktops allow far more): vkr_allocate logs once when the
     * device's limit is reached and refuses only under ICO_VK_FAKE_LIMITS
     * (vkr_memory_room; rhi_create_sampler does the same for samplers).  The
     * counts reach the 10-second log through rhi_get_stats
     * (RhiStats.memoryLive...). */
    uint32_t memLive, memPeak, samplersLive;
    uint64_t memLiveBytes, memPeakBytes;
    uint32_t memNextLog; /* the live count whose crossing logs next */
    bool memLimitLogged, samplerLimitLogged;
    /* vkr_allocate's fallback: the small device-local, host-visible
     * windows (vkr_memory_small_window) that refused an allocation, a bit
     * per memory type, tried last from then on; the kinds (RhiMemory) whose
     * first fallback was logged */
    uint32_t memTypeFull;
    bool memFallbackLogged[3];
    /* ICO_VK_FAKE_LIMITS (vk_device.c vkr_fake_limits): props.limits were
     * clamped to a named device's (mali); fakeMinLimits: =min, the
     * RhiLimits the renderer reads are lowered to the spec's required
     * values in vkr_fill_limits (props.limits are left as they are) */
    const char *fakeLimits;
    bool fakeMinLimits;
    const char *lastLimit; /* vkr_test_last_limit (rhi_vk.h) */
} VkrState;

extern VkrState g_vkr;

/* the Vulkan format of an RhiFormat; RHI_FMT_D32F_S8 resolves to the chosen
 * depth-stencil format */
static inline VkFormat vkr_vk_format(RhiFormat f)
{
    return vkr_vk_format_with(f, g_vkr.dsFormat);
}

#define VKR_LOG(...)                                                                               \
    do {                                                                                           \
        fprintf(stderr, "rhi_vk: " __VA_ARGS__);                                                   \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define VKR_CHECK(expr) vkr_check((expr), #expr, __FILE__, __LINE__)

bool vkr_check(VkResult r, const char *what, const char *file, int line);
/* A monotonic clock in ns, for the blocked-time counters (RhiStats). */
uint64_t vkr_now_ns(void);

/* The recording thread's frame slot (rhi_frame_slot).  Only the thread
 * that calls rhi_wait_frame may ask: the end, the submit and the present of
 * a list take the list's own slot (VkrCmdList.slot, the acquire's), as the
 * submit thread makes them while the recording thread has moved on */
static inline VkrFrame *vkr_cur_frame(void)
{
    return &g_vkr.frames[g_vkr.frameIndex % RHI_FRAMES_IN_FLIGHT];
}

/* a handle's tag names its slot: the slot is the tag modulo the slots */
_Static_assert(((VKR_GEN_MASK + 1u) % RHI_FRAMES_IN_FLIGHT) == 0u,
               "a command list's frame tag must name its slot");

/* VkrState.deferred: what a present on the submit thread left for the next
 * acquire (vk_swapchain.c) */
#define VKR_DEFER_REBUILD 1u      /* out of date (or suboptimal on the desktop) */
#define VKR_DEFER_SIZE 2u         /* Android's suboptimal with another size */
#define VKR_DEFER_POLL 4u         /* the poll found another size or turn (deferW x deferH) */
#define VKR_DEFER_SURFACE_LOST 8u /* the surface went away */

/* the late counters (VkrState.late) */
static inline void vkr_late_add(uint64_t *counter, uint64_t n)
{
    __atomic_fetch_add(counter, n, __ATOMIC_RELAXED);
}

/* vk_device.c: the device-loss report (VkrState, "GPU loss diagnostics"),
 * once, from vkr_check at the first VK_ERROR_DEVICE_LOST */
void vkr_lost_report(void);
/* vk_cmd.c: a submission of slot f into VkrState.submits, with the slot's
 * tag (rhi_set_submit_tag) */
void vkr_note_submit(const VkrFrame *f, uint64_t value, uint32_t lists, bool present);
/* vk_cmd.c: blocks until submission `value` of the port's counter has
 * finished (0, or a lost device: returns at once), counted and seen by the
 * watchdog (rhi_gpu_progress); a failed wait is logged and noted
 * (lostWaitValue) */
void vkr_wait_value(uint64_t value);

/* vk_sync.c, the submission sync (VkrSyncKind):
 *   vkr_sync_init     the timeline semaphore or the fence ring of
 *                     g_vkr.syncKind, after the device (false: the call
 *                     that failed is logged);
 *   vkr_sync_submit   one vkQueueSubmit of d signalling the counter's next
 *                     number, *outValue (timelineValue moves to it only
 *                     when the submit succeeded); on the fence path a slot
 *                     still pending is waited for first (vkr_wait_value);
 *   vkr_sync_reached  the newest number finished, without blocking; false
 *                     when the driver could not say (*out then the newest
 *                     known finished, 0 on the timeline path);
 *   vkr_sync_wait     blocks until `value` has finished (VK_SUCCESS) or the
 *                     wait failed (its VkResult);
 *   vkr_sync_destroy  the semaphore or the fences, with the device idle;
 *   vkr_sync_name     "timeline" or "fences". */
bool vkr_sync_init(void);
bool vkr_sync_submit(const VkrSubmitDesc *d, uint64_t *outValue);
bool vkr_sync_reached(uint64_t *out);
VkResult vkr_sync_wait(uint64_t value);
void vkr_sync_destroy(void);
const char *vkr_sync_name(void);
/* vk_renderpass.c, the render passes (VkrPassDesc; the file's header has
 * the caches):
 *   vkr_pass_begin     records the pass's begin on c: vkCmdBeginRendering
 *                      (dynamic rendering) or vkCmdBeginRenderPass with the
 *                      cached VkRenderPass and VkFramebuffer
 *                      (g_vkr.legacyPasses); false (logged) when the legacy
 *                      objects could not be made;
 *   vkr_pass_end       its end;
 *   vkr_renderpass_get the cached VkRenderPass of d (legacy);
 *   vkr_renderpass_compat the render pass of a compatibility class (the
 *                      formats and one sample count; depthFormat
 *                      VK_FORMAT_UNDEFINED: none), for a pipeline's
 *                      create info on the legacy path; VK_NULL_HANDLE when
 *                      it could not be made;
 *   vkr_framebuffer_forget_view the cached framebuffers using view go:
 *                      deferred (rhi_destroy_texture, before its views), or
 *                      at once with the device idle (now: a swapchain
 *                      image's release);
 *   vkr_renderpass_shutdown every cached object destroyed, the device idle. */
bool vkr_pass_begin(VkrCmdList *c, const VkrPassDesc *d);
void vkr_pass_end(VkrCmdList *c);
VkRenderPass vkr_renderpass_get(const VkrPassDesc *d);
VkRenderPass vkr_renderpass_compat(uint32_t colorCount, const VkFormat *colorFormats,
                                   VkFormat depthFormat, VkSampleCountFlagBits samples);
void vkr_framebuffer_forget_view(VkImageView view, bool now);
void vkr_renderpass_shutdown(void);
/* vk_cmd.c: rhi_gpu_progress's values back to none (a new device) */
void vkr_progress_reset(void);

/* vk_resource.c: the allocation table, every VkDeviceMemory alive with the
 * object bound to it, its size and debug name, and (with the address
 * binding report) the object's GPU address range.  vkr_alloc_note when
 * vkr_allocate succeeds, vkr_alloc_forget in vkr_free_memory,
 * vkr_alloc_bound from the report's callback (vk_device.c);
 * vkr_alloc_nearest logs the resource whose range holds `address`, or the
 * nearest below it. */
void vkr_alloc_note(VkDeviceMemory memory, uint64_t object, VkDeviceSize size, const char *name);
void vkr_alloc_forget(VkDeviceMemory memory);
void vkr_alloc_bound(uint64_t object, uint64_t base, uint64_t size, bool bound);
void vkr_alloc_nearest(uint64_t address);
void vkr_alloc_table_free(void);

/* vk_resource.c */
void vkr_defer(VkrGarbageKind kind, uint64_t handle);
/* a device memory object's deferred free, with its size for the counts */
void vkr_defer_memory(VkDeviceMemory memory, VkDeviceSize size);
/* vkFreeMemory and the live counts (every free goes through it) */
void vkr_free_memory(VkDeviceMemory memory, VkDeviceSize size);
void vkr_destroy_garbage(VkrFrame *f);
/* vk_cmd.c: an empty submit that waits on slot f's pending acquire */
bool vkr_submit_empty(VkrFrame *f);
VkrBuffer *vkr_get_buffer(RhiBuffer b);
VkrTexture *vkr_get_texture(RhiTexture t);

uint32_t vkr_register_swapchain_image(VkImage image, VkFormat fmt, RhiFormat rf, uint32_t w,
                                      uint32_t h);

void vkr_release_swapchain_image(uint32_t id);
void vkr_release_all_objects(void);
/* vk_pipeline.c: the pipeline cache (rhi_set_pipeline_cache_path): created
 * after the device, from the file when its header names this device;
 * saved to the file and destroyed before the device goes */
void vkr_pipeline_cache_init(void);
void vkr_pipeline_cache_shutdown(void);
/* vk_pipeline.c, issue 188: on a driver whose compiler is not thread-safe
 * (rhi_driver_version.h rhi_driver_compiler_serial, RhiLimits.
 * serialPipelines) one compile thread and one lock that
 * vkCreateShaderModule and vkCreateGraphicsPipelines take (vkr_compile_lock,
 * any thread; nothing where the rule does not hold).  vkr_compile_serial_init
 * after the limits are filled (rhi_init; logs the line), _shutdown after the
 * device is gone. */
void vkr_compile_serial_init(void);
void vkr_compile_serial_shutdown(void);
void vkr_compile_lock(void);
void vkr_compile_unlock(void);
/* vk_pipeline.c: the background sets' compiles (rhi.h RhiGpuProgress
 * bgPipesDone, bgPipesLeft); any thread */
void vkr_pipe_background_progress(uint32_t *done, uint32_t *left);
/* vk_pipeline.c */
VkDescriptorSet vkr_get_bind_group(RhiBindGroup bg);
/* The number of dynamic uniform slots of a bind group's layout. */
uint32_t vkr_bind_group_dynamic_count(RhiBindGroup bg);
/* vk_cmd.c */
bool vkr_frames_init(void);
/* Empty submit after slot f's work that signals its image's present
 * semaphore (rhi_present), when no list's submit signalled it
 * (VkrAcquire.signalled). */
bool vkr_submit_present_signal(VkrFrame *f);
void vkr_frames_shutdown(void);
VkrCmdList *vkr_get_cmd(RhiCommandList cl);
void vkr_image_barrier(VkrCmdList *c, VkrTexture *t, RhiState before, RhiState after);
/* vk_present_mode.c: the present mode for a swapchain from the surface's
 * modes[n]: FIFO when forceFifo (the FIFO fallback, rhi_present_fifo_fallback);
 * with vsync, MAILBOX when preferMailbox and offered, else FIFO;
 * without, IMMEDIATE when offered, else MAILBOX when offered, else FIFO.
 * vkr_present_mode_name: "immediate", "mailbox", "fifo", "fifo_relaxed", or
 * NULL for any other mode. */
VkPresentModeKHR vkr_choose_present_mode(const VkPresentModeKHR *modes, uint32_t n, bool vsync,
                                         bool preferMailbox, bool preferLatestReady,
                                         bool forceFifo);
const char *vkr_present_mode_name(VkPresentModeKHR m);
/* vk_present_mode.c: whether the display's timing of presents
 * (VK_KHR_present_id + VK_KHR_present_wait, rhi_present_timing) is used:
 * only when asked for (asked: ICO_VK_PRESENT_WAIT, anything but unset, ""
 * or "0"), offered by the driver, and a thread can wait (SDL).  Off by
 * default, opt-in with ICO_VK_PRESENT_WAIT=1: when it was used whenever
 * offered, an RTX 3090 on Windows showed nothing usable in mailbox mode at
 * 120/144/240 and under the vsync lock. */
bool vkr_present_wait_wanted(bool offered, const char *asked, bool canThread);
/* vk_present_mode.c: the quarter turns clockwise (1, 2, 3) a swapchain is
 * made with when the surface's current transform is ROTATE_90, 180 or 270
 * (the system shows the surface turned: an Android phone held sideways),
 * the transform among the supported ones and `on` ([dev] prerotate, not
 * ICO_VK_PREROTATE=0); 0 (identity: the compositor turns each image) for
 * any other transform (identity, the mirrored ones), with `on` false, or
 * when a quarter turn's images (h x w for the current extent w x h) would
 * pass the surface's maxW x maxH.  The swapchain's preTransform is then the
 * current transform and its images the extent turned (vk_swapchain.c) */
uint32_t vkr_prerotate_turns(uint32_t currentTransform, uint32_t supportedTransforms, uint32_t w,
                             uint32_t h, uint32_t maxW, uint32_t maxH, bool on);

/* vk_memory.c: the memory types an allocation of `kind` tries, in order,
 * into out[] (the count returned; distinct type indices of typeBits):
 *   RHI_MEM_DEVICE: device-local, then any;
 *   RHI_MEM_UPLOAD: device-local + host-visible + coherent, then
 *     host-visible + coherent and not device-local (system memory), then
 *     any host-visible + coherent;
 *   RHI_MEM_READBACK: host-visible + coherent + cached, then host-visible +
 *     cached, then host-visible + coherent.
 * An upload over VKR_SMALL_WINDOW_MAX_BYTES skips a small window (a
 * device-local, host-visible type whose heap is under
 * VKR_SMALL_WINDOW_HEAP_BYTES: the PCIe BAR without resizable BAR), and the
 * types in `full` (a bit per type index; vkr_allocate sets a window's bit
 * when it refused an allocation) are skipped too; either comes back at the
 * end of the list as a last resort.  vkr_memory_small_window: whether
 * `type` is such a window. */
#define VKR_SMALL_WINDOW_HEAP_BYTES (1024ull << 20)
#define VKR_SMALL_WINDOW_MAX_BYTES (16ull << 20)
uint32_t vkr_memory_candidates(RhiMemory kind, uint32_t typeBits, VkDeviceSize size,
                               const VkPhysicalDeviceMemoryProperties *props, uint32_t full,
                               uint32_t out[VK_MAX_MEMORY_TYPES]);
bool vkr_memory_small_window(const VkPhysicalDeviceMemoryProperties *props, uint32_t type);

/* vk_swapchain.c */
#ifdef __ANDROID__
/* vk_surface_android.c: a VkSurfaceKHR on the SDL window's ANativeWindow,
 * made with vkCreateAndroidSurfaceKHR from the instance's own
 * vkGetInstanceProcAddr (SDL_Vulkan_CreateSurface would take SDL's loader's,
 * which a driver the program loaded does not answer to).  false when the
 * window has no native window (the app in the background, not logged) or
 * the lookup or the call fails (logged every time; the caller,
 * vk_swapchain.c vkr_surface_create, limits repeats). */
bool vkr_create_window_surface(void *sdlWindow, VkSurfaceKHR *out);
#endif
bool vkr_swapchain_create(uint32_t w, uint32_t h, bool vsync);
void vkr_swapchain_destroy(void);
/* the thread waiting on presents (rhi_present_timing) stopped, before the
 * device goes */
void vkr_present_wait_shutdown(void);
/* vkr_test_fake_present_wait's stand-in, NULL when it is off */
PFN_vkWaitForPresentKHR vkr_present_wait_fake(void);

#endif /* PORT_RHI_VK_VK_INTERNAL_H */

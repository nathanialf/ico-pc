/* vk_internal.h: shared state of the Vulkan backend of port/rhi/rhi.h. */
#ifndef PORT_RHI_VK_VK_INTERNAL_H
#define PORT_RHI_VK_VK_INTERNAL_H

#include "volk.h"
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
    uint32_t next; /* first never-used index */
} VkrPool;

bool vkr_PoolInit(VkrPool *p, const char *name, uint32_t cap, uint32_t elemSize);
void vkr_PoolFree(VkrPool *p);
/* Allocates a zeroed element; returns its id (0 when full). */
uint32_t vkr_PoolAlloc(VkrPool *p, void **out);
void *vkr_PoolGet(const VkrPool *p, uint32_t id);
void vkr_PoolRelease(VkrPool *p, uint32_t id);

/* --------------------------------------------------------------- objects */
typedef struct VkrBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize memSize; /* the allocation's size (vkr_Allocate) */
    uint64_t size;
    RhiMemory kind;
    void *mapped;
    bool coherent;
    /* package PB, hazard tracking (vk_cmd.c, "Hazards"): a copy's write or
     * read not yet behind a barrier, valid while hzEpoch is the recording
     * list's */
    uint64_t hzEpoch;
    bool hzXferWrite, hzXferRead;
} VkrBuffer;

typedef struct VkrTexture {
    VkImage image;
    VkDeviceMemory memory; /* VK_NULL_HANDLE for swapchain images */
    VkDeviceSize memSize;  /* the allocation's size (vkr_Allocate) */
    VkImageView view;      /* all aspects: attachments, colour sampling */
    VkImageView depthView; /* depth aspect only, depth-stencil formats that are sampled */
    VkFormat format;
    RhiFormat rhiFormat;
    VkImageAspectFlags aspects;
    uint32_t width, height, mips;
    bool swapchain;
    /* package PB, hazard tracking (vk_cmd.c, "Hazards"): what touched the
     * image since its last barrier, valid while hzEpoch is the recording
     * list's: the mips a copy wrote, and an attachment write or read */
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
    uint32_t dynamicCount; /* RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots (package PA) */
} VkrLayout;

typedef struct VkrPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    uint32_t layoutCount;
} VkrPipeline;

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
    VKR_GARBAGE_SHADER
} VkrGarbageKind;

typedef struct VkrGarbage {
    VkrGarbageKind kind;
    uint64_t handle; /* any non-dispatchable Vulkan handle */
    uint64_t size;   /* VKR_GARBAGE_MEMORY: the allocation's size (vkr_DeferMemory) */
    uint32_t slot;   /* overflow entries only: the frame slot that deferred it */
} VkrGarbage;

/* Deferred destroys that found their frame's garbage list unable to grow
 * (out of memory): kept here, per entry with its frame slot, and destroyed
 * with that slot's own garbage (vkr_DestroyGarbage). */
#define VKR_GARBAGE_OVERFLOW 256

#define VKR_MAX_CMD_LISTS 8
/* image barriers a command list holds back before issuing them together
 * (vk_cmd.c, vkr_FlushBarriers) */
#define VKR_PENDING_BARRIERS 8
#define VKR_DESC_POOLS_MAX 16
/* swapchain images: the most a surface's swapchain may have (vk_swapchain.c) */
#define VKR_MAX_SWAP_IMAGES 16u

typedef struct VkrCmdList {
    VkCommandBuffer cb;
    bool recording;
    bool submitted;
    /* draw-time state */
    VkrPipeline *pipeline;
    VkDescriptorSet groups[RHI_MAX_BIND_SLOTS];
    /* package PA: each bound group's dynamic offsets, in binding order */
    uint32_t dynCount[RHI_MAX_BIND_SLOTS];
    uint32_t offsets[RHI_MAX_BIND_SLOTS][RHI_MAX_DYNAMIC_OFFSETS];
    uint32_t groupDirty; /* bit per group */
    bool inPass;
    /* package PB: the list's hazard-tracking epoch (g_vkr.hzEpoch when it
     * began); globalOrder: a global barrier before every pass and copy
     * instead (ICO_VK_GLOBAL_BARRIERS=1, or lists recorded interleaved) */
    uint64_t epoch;
    bool globalOrder;
    /* image barriers recorded by rhi_CmdBarrier and not issued yet, with
     * their stage masks OR-ed: they go out in one vkCmdPipelineBarrier
     * before the list's next command (vk_cmd.c, vkr_FlushBarriers) */
    VkImageMemoryBarrier pendImg[VKR_PENDING_BARRIERS];
    uint32_t pendCount;
    VkPipelineStageFlags pendSrc, pendDst;
} VkrCmdList;

typedef struct VkrFrame {
    uint64_t waitValue; /* timeline value of the frame's last submit */
    VkCommandPool cmdPool;
    VkrCmdList lists[VKR_MAX_CMD_LISTS];
    uint32_t listCount;
    VkDescriptorPool descPools[VKR_DESC_POOLS_MAX];
    uint32_t descPoolCount; /* pools created */
    uint32_t descPoolCur;   /* pool being allocated from */
    VkDescriptorSet *sets;  /* transient bind groups of this frame */
    uint8_t *setDynamic;    /* per set: its layout's dynamic slots (package PA) */
    uint32_t setCount, setCap;
    VkrGarbage *garbage;
    uint32_t garbageCount, garbageCap;
    VkSemaphore acquireSem; /* swapchain image acquire */
    /* package P1: the slot's GPU timestamps (rhi_CmdWriteTimestamp) */
    VkQueryPool queryPool;
    uint32_t tsWritten; /* bit per index written since the slot was recycled */
    bool tsReset;       /* the pool's reset is recorded in this slot's first list */
} VkrFrame;

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
    /* package AN-F: the Vulkan format behind RHI_FMT_D32F_S8 (D32_SFLOAT_S8_UINT,
     * or D24_UNORM_S8_UINT without it or under ICO_VK_FAKE_D24S8) */
    VkFormat dsFormat;
    bool dualSrcBlend; /* package AN-E: the feature enabled (RhiLimits.dualSourceBlend) */
    /* texture packs: textureCompressionBC enabled and the four BC formats
     * sampleable and copyable (RhiLimits.bcTextures) */
    bool bc;
    uint32_t validationErrors;
    bool deviceLost; /* a call returned VK_ERROR_DEVICE_LOST (rhi_DeviceLost) */

    /* timeline semaphore: one value per submit */
    VkSemaphore timeline;
    uint64_t timelineValue;

    /* frames */
    VkrFrame frames[RHI_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;

    /* objects */
    VkrPool buffers, textures, samplers, shaders, layouts, pipelines;

    /* swapchain (absent when headless) */
    void *window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapFormat;
    RhiFormat swapRhiFormat;
    VkColorSpaceKHR swapColorSpace;
    uint32_t swapWidth, swapHeight;
    bool vsync;
    uint32_t swapImageCount;
    uint32_t swapTextures[VKR_MAX_SWAP_IMAGES];  /* texture ids */
    VkSemaphore renderDone[VKR_MAX_SWAP_IMAGES]; /* per image */
    uint32_t swapImage;                          /* acquired index */
    bool swapAcquired;                           /* image acquired, not yet presented */
    bool acquireWaitPending; /* acquire semaphore not yet waited on by a submit */
    VkSemaphore acquireSem;  /* the semaphore that acquire signals */

    /* one-shot command pool for readback */
    VkCommandPool oneShotPool;

    /* every pipeline is created through it (vkr_PipelineCacheInit) */
    VkPipelineCache pipelineCache;

    /* package P1: counters (rhi_GetStats), timestamps, the mailbox option */
    RhiStats stats;
    bool timestamps;        /* the queue writes timestamps */
    float timestampPeriod;  /* ns per tick */
    uint64_t timestampMask; /* the valid bits */
    uint64_t tsResult[RHI_MAX_TIMESTAMPS];
    uint32_t tsCount; /* of the slot rhi_WaitFrame recycled last */
    bool mailbox;     /* the swapchain presents in mailbox mode (rhi_PreferMailbox) */

    /* v0.3.1: the swapchain's present mode (rhi_PresentModeName) */
    VkPresentModeKHR presentMode;

    /* package PB: hazard tracking (vk_cmd.c, "Hazards") */
    bool globalBarriers; /* ICO_VK_GLOBAL_BARRIERS=1: main's global barrier path */
    uint64_t hzEpoch;    /* the last command list's epoch (one per rhi_BeginCommands) */

    VkrGarbage overflow[VKR_GARBAGE_OVERFLOW]; /* see VKR_GARBAGE_OVERFLOW */
    uint32_t overflowCount;

    /* v0.4.2 (Android): the device memory objects alive and their bytes,
     * with the peaks, and the samplers alive.  Every buffer and texture has
     * its own allocation (vk_resource.c), and maxMemoryAllocationCount is
     * 4096 on many phone GPUs (desktops allow far more): vkr_Allocate
     * refuses past the device's limits with a log line instead of asking
     * the driver for what the spec does not allow, and the counts reach
     * the 10-second log through rhi_GetStats (RhiStats.memoryLive...). */
    uint32_t memLive, memPeak, samplersLive;
    uint64_t memLiveBytes, memPeakBytes;
    uint32_t memNextLog; /* the live count whose crossing logs next */
    bool memLimitLogged, samplerLimitLogged;
    /* ICO_VK_FAKE_LIMITS (vk_device.c vkr_FakeLimits): props.limits were
     * clamped to a named device's (mali); fakeMinLimits: =min, the
     * RhiLimits the renderer reads are lowered to the spec's required
     * values in vkr_FillLimits (props.limits are left as they are) */
    const char *fakeLimits;
    bool fakeMinLimits;
    const char *lastLimit; /* vkr_TestLastLimit (rhi_vk.h) */
} VkrState;

extern VkrState g_vkr;

/* the Vulkan format of an RhiFormat; RHI_FMT_D32F_S8 resolves to the chosen
 * depth-stencil format (package AN-F) */
static inline VkFormat vkr_VkFormat(RhiFormat f)
{
    return vkr_VkFormatWith(f, g_vkr.dsFormat);
}

#define VKR_LOG(...)                                                                               \
    do {                                                                                           \
        fprintf(stderr, "rhi_vk: " __VA_ARGS__);                                                   \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define VKR_CHECK(expr) vkr_Check((expr), #expr, __FILE__, __LINE__)

bool vkr_Check(VkResult r, const char *what, const char *file, int line);
/* A monotonic clock in ns, for the blocked-time counters (RhiStats). */
uint64_t vkr_NowNs(void);

static inline VkrFrame *vkr_CurFrame(void)
{
    return &g_vkr.frames[g_vkr.frameIndex % RHI_FRAMES_IN_FLIGHT];
}

/* vk_resource.c */
bool vkr_FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid,
                        uint32_t *out);

void vkr_Defer(VkrGarbageKind kind, uint64_t handle);
/* a device memory object's deferred free, with its size for the counts */
void vkr_DeferMemory(VkDeviceMemory memory, VkDeviceSize size);
/* vkFreeMemory and the live counts (every free goes through it) */
void vkr_FreeMemory(VkDeviceMemory memory, VkDeviceSize size);
void vkr_DestroyGarbage(VkrFrame *f);
bool vkr_SubmitEmpty(void); /* vk_cmd.c: an empty submit that waits on a pending acquire */
VkrBuffer *vkr_GetBuffer(RhiBuffer b);
VkrTexture *vkr_GetTexture(RhiTexture t);

uint32_t vkr_RegisterSwapchainImage(VkImage image, VkFormat fmt, RhiFormat rf, uint32_t w,
                                    uint32_t h);

void vkr_ReleaseSwapchainImage(uint32_t id);
void vkr_ReleaseAllObjects(void);
/* vk_pipeline.c: the pipeline cache (rhi_SetPipelineCachePath): created
 * after the device, from the file when its header names this device;
 * saved to the file and destroyed before the device goes */
void vkr_PipelineCacheInit(void);
void vkr_PipelineCacheShutdown(void);
/* vk_pipeline.c */
VkDescriptorSet vkr_GetBindGroup(RhiBindGroup bg);
/* The number of dynamic uniform slots of a bind group's layout (package PA). */
uint32_t vkr_BindGroupDynamicCount(RhiBindGroup bg);
/* vk_cmd.c */
bool vkr_FramesInit(void);
/* Empty submit after the frame's work that signals the image's present
 * semaphore (rhi_Present). */
bool vkr_SubmitPresentSignal(void);
void vkr_FramesShutdown(void);
VkrCmdList *vkr_GetCmd(RhiCommandList cl);
void vkr_ImageBarrier(VkrCmdList *c, VkrTexture *t, RhiState before, RhiState after);
/* vk_present_mode.c: the present mode for a swapchain from the surface's
 * modes[n]: with vsync, MAILBOX when preferMailbox and offered, else FIFO;
 * without, IMMEDIATE when offered, else MAILBOX when offered, else FIFO.
 * vkr_PresentModeName: "immediate", "mailbox", "fifo", "fifo_relaxed", or
 * NULL for any other mode. */
VkPresentModeKHR vkr_ChoosePresentMode(const VkPresentModeKHR *modes, uint32_t n, bool vsync,
                                       bool preferMailbox);
const char *vkr_PresentModeName(VkPresentModeKHR m);

/* vk_swapchain.c */
#ifdef __ANDROID__
/* vk_surface_android.c (v0.4.3 AN-22a): a VkSurfaceKHR on the SDL window's
 * ANativeWindow, made with vkCreateAndroidSurfaceKHR from the instance's own
 * vkGetInstanceProcAddr (SDL_Vulkan_CreateSurface would take SDL's loader's,
 * which a driver the program loaded does not answer to).  false (logged
 * once per failure kind) when the window has no native window (the app in
 * the background) or the call fails. */
bool vkr_CreateWindowSurface(void *sdlWindow, VkSurfaceKHR *out);
#endif
bool vkr_SwapchainCreate(uint32_t w, uint32_t h, bool vsync);
void vkr_SwapchainDestroy(void);

#endif /* PORT_RHI_VK_VK_INTERNAL_H */

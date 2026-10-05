/* vk_internal.h: shared state of the Vulkan backend of port/rhi/rhi.h.
 * port/rhi/vk/README.md describes the design. */
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
    uint64_t size;
    RhiMemory kind;
    void *mapped;
    bool coherent;
} VkrBuffer;

typedef struct VkrTexture {
    VkImage image;
    VkDeviceMemory memory; /* VK_NULL_HANDLE for swapchain images */
    VkImageView view;      /* all aspects: attachments, colour sampling */
    VkImageView depthView; /* depth aspect only, depth-stencil formats that are sampled */
    VkFormat format;
    RhiFormat rhiFormat;
    VkImageAspectFlags aspects;
    uint32_t width, height, mips;
    bool swapchain;
} VkrTexture;

typedef struct VkrShader {
    VkShaderModule module;
    RhiShaderStage stage;
    char entry[64];
} VkrShader;

typedef struct VkrLayout {
    VkDescriptorSetLayout layout;
    uint32_t slotCount;
    RhiBindSlot slots[16];
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
} VkrGarbage;

#define VKR_MAX_CMD_LISTS 8
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
    uint32_t groupDirty; /* bit per group */
    bool inPass;
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

    /* package P1: counters (rhi_GetStats), timestamps, the mailbox option */
    RhiStats stats;
    bool timestamps;        /* the queue writes timestamps */
    float timestampPeriod;  /* ns per tick */
    uint64_t timestampMask; /* the valid bits */
    uint64_t tsResult[RHI_MAX_TIMESTAMPS];
    uint32_t tsCount; /* of the slot rhi_WaitFrame recycled last */
    bool mailbox;     /* the swapchain presents in mailbox mode (rhi_PreferMailbox) */
} VkrState;

extern VkrState g_vkr;

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
void vkr_DestroyGarbage(VkrFrame *f);
VkrBuffer *vkr_GetBuffer(RhiBuffer b);
VkrTexture *vkr_GetTexture(RhiTexture t);

uint32_t vkr_RegisterSwapchainImage(VkImage image, VkFormat fmt, RhiFormat rf, uint32_t w,
                                    uint32_t h);

void vkr_ReleaseSwapchainImage(uint32_t id);
void vkr_ReleaseAllObjects(void);
/* vk_pipeline.c */
VkDescriptorSet vkr_GetBindGroup(RhiBindGroup bg);
/* vk_cmd.c */
bool vkr_FramesInit(void);
/* Empty submit after the frame's work that signals the image's present
 * semaphore (rhi_Present). */
bool vkr_SubmitPresentSignal(void);
void vkr_FramesShutdown(void);
VkrCmdList *vkr_GetCmd(RhiCommandList cl);
void vkr_ImageBarrier(VkCommandBuffer cb, VkrTexture *t, RhiState before, RhiState after);
/* vk_swapchain.c */
bool vkr_SwapchainCreate(uint32_t w, uint32_t h, bool vsync);
void vkr_SwapchainDestroy(void);

#endif /* PORT_RHI_VK_VK_INTERNAL_H */

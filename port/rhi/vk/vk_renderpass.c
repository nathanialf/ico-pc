/* vk_renderpass.c: the render passes of the Vulkan backend, both paths
 * (vk_internal.h, VkrPassDesc).
 *
 *   dynamic  (core 1.3 or VK_KHR_dynamic_rendering) vkr_pass_begin builds
 *            the VkRenderingInfo the backend has always recorded, field for
 *            field, and calls vkCmdBeginRendering.
 *   legacy   (g_vkr.legacyPasses: a device without dynamic rendering, a
 *            stock Vulkan 1.1 driver; ICO_VK_FAKE_API=1.1) a classic
 *            VkRenderPass and VkFramebuffer, both from caches here, and
 *            vkCmdBeginRenderPass.
 *
 * A legacy render pass has one subpass, no dependencies, no resolve or
 * input attachments.  Every attachment's initial, subpass and final layout
 * is the layout the port's state tracking put it in before the pass
 * (vk_cmd.c's hazard barriers, or rhi_cmd_barrier under the global
 * barriers), so the pass makes no layout transition and needs no
 * dependency: the barriers before it order it, as on the dynamic path.  A
 * pass without attachments (a storage pass) is a subpass without
 * attachments, its framebuffer the render area's size.
 *
 * The caches are linear arrays, searched by memcmp of a zeroed key:
 *   render passes  VKR_RP_CACHE entries, keyed by the attachments' formats,
 *                  sample counts, ops and layouts.  The compatibility class
 *                  of a pipeline or a framebuffer (formats and samples
 *                  only) is a render pass of the same cache with fixed ops
 *                  and layouts (vkr_renderpass_compat).  Never evicted and
 *                  never destroyed before the device goes: an asynchronous
 *                  pipeline compile may hold a pass of any age.  Past
 *                  VKR_RP_CACHE entries the table doubles on the heap
 *                  (logged once).
 *   framebuffers   VKR_FB_CACHE entries, keyed by the compatible render
 *                  pass, the views and the size.  An entry whose view is
 *                  destroyed goes with it (vkr_framebuffer_forget_view:
 *                  deferred before the texture's views, at once for a
 *                  swapchain image after the device is idle); when the
 *                  cache is full the least recently used entry is deferred
 *                  (logged once).
 * Both are used on the thread that records and creates pipelines (the
 * RHI's one thread); a compile thread only reads the immutable handle
 * vkr_pipe_prepare put in its create info. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

#define VKR_H(x) ((uint64_t)(x))

#define VKR_RP_CACHE 128u
#define VKR_FB_CACHE 256u
#define VKR_RP_ATTACH (RHI_MAX_COLOR_TARGETS + 1u)

/* zeroed before it is filled: compared with memcmp (VkAttachmentDescription
 * is nine 32-bit members, no padding) */
typedef struct VkrRpKey {
    uint32_t colorCount;
    uint32_t hasDepth;
    VkAttachmentDescription att[VKR_RP_ATTACH];
} VkrRpKey;

typedef struct VkrRpEntry {
    VkrRpKey key;
    VkRenderPass pass;
} VkrRpEntry;

typedef struct VkrFbKey {
    VkRenderPass compat;
    uint32_t count;
    uint32_t width, height;
    VkImageView views[VKR_RP_ATTACH];
} VkrFbKey;

typedef struct VkrFbEntry {
    VkrFbKey key;
    VkFramebuffer fb;
    uint64_t lastUse;
} VkrFbEntry;

static VkrRpEntry s_rpStatic[VKR_RP_CACHE];
static VkrRpEntry *s_rp = s_rpStatic; /* s_rpStatic, then the heap once it is full */
static uint32_t s_rpCap = VKR_RP_CACHE;
static uint32_t s_rpCount;
static VkrFbEntry s_fb[VKR_FB_CACHE];
static uint32_t s_fbCount;
static uint64_t s_fbClock;
static bool s_rpFullLogged, s_fbFullLogged, s_firstLogged;

/* ------------------------------------------------------------ render passes */
static VkRenderPass vkr_rp_make(const VkrRpKey *k)
{
    const uint32_t n = k->colorCount + (k->hasDepth ? 1u : 0u);
    VkAttachmentReference colorRef[RHI_MAX_COLOR_TARGETS];
    VkAttachmentReference depthRef;
    for (uint32_t i = 0; i < k->colorCount; i++) {
        colorRef[i] = (VkAttachmentReference){i, k->att[i].initialLayout};
    }
    if (k->hasDepth) {
        depthRef = (VkAttachmentReference){k->colorCount, k->att[k->colorCount].initialLayout};
    }
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = k->colorCount,
        .pColorAttachments = k->colorCount ? colorRef : NULL,
        .pDepthStencilAttachment = k->hasDepth ? &depthRef : NULL,
    };
    VkRenderPassCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = n,
        .pAttachments = n ? k->att : NULL,
        .subpassCount = 1,
        .pSubpasses = &sub,
    };
    VkRenderPass rp = VK_NULL_HANDLE;
    if (!VKR_CHECK(vkCreateRenderPass(g_vkr.device, &ci, NULL, &rp))) {
        return VK_NULL_HANDLE;
    }
    if (!s_firstLogged) {
        s_firstLogged = true;
        VKR_LOG("render passes: VkRenderPass objects (no dynamic rendering), the first with %u "
                "colour target(s)%s; read-only depth stored with %s",
                k->colorCount, k->hasDepth ? " and a depth target" : "",
                g_vkr.storeOpNone ? "STORE_OP_NONE" : "STORE (no store op none)");
    }
    return rp;
}

static VkRenderPass vkr_rp_lookup(const VkrRpKey *k)
{
    for (uint32_t i = 0; i < s_rpCount; i++) {
        if (memcmp(&s_rp[i].key, k, sizeof(*k)) == 0) {
            return s_rp[i].pass;
        }
    }
    VkRenderPass rp = vkr_rp_make(k);
    if (!rp) {
        return VK_NULL_HANDLE;
    }
    if (s_rpCount == s_rpCap) {
        /* a compatible pass may be handed to an asynchronous pipeline compile,
         * so no pass is ever destroyed before the device: the table grows */
        if (!s_rpFullLogged) {
            s_rpFullLogged = true;
            VKR_LOG("render pass cache full (%u): the table grows, passes are kept", VKR_RP_CACHE);
        }
        VkrRpEntry *grown = malloc((size_t)s_rpCap * 2u * sizeof(*grown));
        if (!grown) {
            /* out of memory: the old one-use rule, the frame's commands only */
            vkr_defer(VKR_GARBAGE_RENDERPASS, VKR_H(rp));
            return rp;
        }
        memcpy(grown, s_rp, (size_t)s_rpCount * sizeof(*grown));
        if (s_rp != s_rpStatic) {
            free(s_rp);
        }
        s_rp = grown;
        s_rpCap *= 2u;
    }
    s_rp[s_rpCount].key = *k;
    s_rp[s_rpCount].pass = rp;
    s_rpCount++;
    return rp;
}

static void vkr_rp_attach(VkAttachmentDescription *a, const VkrPassAttach *p)
{
    a->format = p->format;
    a->samples = p->samples;
    a->loadOp = p->load;
    a->storeOp = p->store;
    a->stencilLoadOp = p->stencilLoad;
    a->stencilStoreOp = p->stencilStore;
    a->initialLayout = p->layout;
    a->finalLayout = p->layout;
}

VkRenderPass vkr_renderpass_get(const VkrPassDesc *d)
{
    VkrRpKey k;
    memset(&k, 0, sizeof(k));
    k.colorCount = d->colorCount;
    k.hasDepth = d->hasDepth ? 1u : 0u;
    for (uint32_t i = 0; i < d->colorCount; i++) {
        vkr_rp_attach(&k.att[i], &d->color[i]);
    }
    if (d->hasDepth) {
        vkr_rp_attach(&k.att[d->colorCount], &d->depth);
    }
    return vkr_rp_lookup(&k);
}

VkRenderPass vkr_renderpass_compat(uint32_t colorCount, const VkFormat *colorFormats,
                                   VkFormat depthFormat, VkSampleCountFlagBits samples)
{
    if (colorCount > RHI_MAX_COLOR_TARGETS) {
        return VK_NULL_HANDLE;
    }
    VkrRpKey k;
    memset(&k, 0, sizeof(k));
    k.colorCount = colorCount;
    k.hasDepth = depthFormat != VK_FORMAT_UNDEFINED ? 1u : 0u;
    /* compatibility is the formats and the sample counts: the ops and
     * layouts are fixed, so every pass of one class shares this one */
    for (uint32_t i = 0; i < colorCount + k.hasDepth; i++) {
        VkAttachmentDescription *a = &k.att[i];
        const bool depth = i == colorCount;
        a->format = depth ? depthFormat : colorFormats[i];
        a->samples = samples;
        a->loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a->storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a->initialLayout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a->finalLayout = a->initialLayout;
    }
    return vkr_rp_lookup(&k);
}

/* ------------------------------------------------------------- framebuffers */
static VkFramebuffer vkr_framebuffer_get(const VkrPassDesc *d)
{
    VkFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < d->colorCount; i++) {
        colorFormats[i] = d->color[i].format;
    }
    /* a subpass's attachments share one sample count; none: 1 */
    const VkSampleCountFlagBits samples = d->colorCount ? d->color[0].samples
                                          : d->hasDepth ? d->depth.samples
                                                        : VK_SAMPLE_COUNT_1_BIT;
    VkrFbKey k;
    memset(&k, 0, sizeof(k));
    k.compat = vkr_renderpass_compat(d->colorCount, colorFormats,
                                     d->hasDepth ? d->depth.format : VK_FORMAT_UNDEFINED, samples);
    if (!k.compat) {
        return VK_NULL_HANDLE;
    }
    k.width = d->fbW;
    k.height = d->fbH;
    for (uint32_t i = 0; i < d->colorCount; i++) {
        k.views[k.count++] = d->color[i].view;
    }
    if (d->hasDepth) {
        k.views[k.count++] = d->depth.view;
    }
    s_fbClock++;
    for (uint32_t i = 0; i < s_fbCount; i++) {
        if (memcmp(&s_fb[i].key, &k, sizeof(k)) == 0) {
            s_fb[i].lastUse = s_fbClock;
            return s_fb[i].fb;
        }
    }
    VkFramebufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = k.compat,
        .attachmentCount = k.count,
        .pAttachments = k.count ? k.views : NULL,
        .width = k.width,
        .height = k.height,
        .layers = 1,
    };
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (!VKR_CHECK(vkCreateFramebuffer(g_vkr.device, &ci, NULL, &fb))) {
        return VK_NULL_HANDLE;
    }
    uint32_t slot = s_fbCount;
    if (s_fbCount == VKR_FB_CACHE) {
        /* the least recently used one goes once the commands that may use
         * it have finished */
        slot = 0;
        for (uint32_t i = 1; i < s_fbCount; i++) {
            if (s_fb[i].lastUse < s_fb[slot].lastUse) {
                slot = i;
            }
        }
        if (!s_fbFullLogged) {
            s_fbFullLogged = true;
            VKR_LOG("framebuffer cache full (%u): the least recently used ones are replaced "
                    "(logged once)",
                    VKR_FB_CACHE);
        }
        vkr_defer(VKR_GARBAGE_FRAMEBUFFER, VKR_H(s_fb[slot].fb));
    } else {
        s_fbCount++;
    }
    s_fb[slot].key = k;
    s_fb[slot].fb = fb;
    s_fb[slot].lastUse = s_fbClock;
    return fb;
}

void vkr_framebuffer_forget_view(VkImageView view, bool now)
{
    if (!view) {
        return;
    }
    for (uint32_t i = 0; i < s_fbCount;) {
        bool uses = false;
        for (uint32_t v = 0; v < s_fb[i].key.count; v++) {
            uses = uses || s_fb[i].key.views[v] == view;
        }
        if (!uses) {
            i++;
            continue;
        }
        if (now) {
            vkDestroyFramebuffer(g_vkr.device, s_fb[i].fb, NULL);
        } else {
            vkr_defer(VKR_GARBAGE_FRAMEBUFFER, VKR_H(s_fb[i].fb));
        }
        s_fb[i] = s_fb[--s_fbCount];
    }
}

void vkr_renderpass_shutdown(void)
{
    for (uint32_t i = 0; i < s_fbCount; i++) {
        vkDestroyFramebuffer(g_vkr.device, s_fb[i].fb, NULL);
    }
    for (uint32_t i = 0; i < s_rpCount; i++) {
        vkDestroyRenderPass(g_vkr.device, s_rp[i].pass, NULL);
    }
    s_fbCount = s_rpCount = 0;
    if (s_rp != s_rpStatic) {
        free(s_rp);
    }
    s_rp = s_rpStatic;
    s_rpCap = VKR_RP_CACHE;
    s_fbClock = 0;
    s_rpFullLogged = s_fbFullLogged = s_firstLogged = false;
}

/* ------------------------------------------------------------ begin and end */
bool vkr_pass_begin(VkrCmdList *c, const VkrPassDesc *d)
{
    if (g_vkr.legacyPasses) {
        const VkRenderPass rp = vkr_renderpass_get(d);
        const VkFramebuffer fb = rp ? vkr_framebuffer_get(d) : VK_NULL_HANDLE;
        if (!fb) {
            static uint32_t s_beginFailed;
            if (s_beginFailed < 8u) { /* the first 8 only; the pass's draws are then skipped */
                s_beginFailed++;
                VKR_LOG("render pass: no VkRenderPass or framebuffer for %u colour target(s)%s "
                        "(the pass's draws are skipped)%s",
                        d->colorCount, d->hasDepth ? " and a depth target" : "",
                        s_beginFailed == 8u ? "; further such lines are not logged" : "");
            }
            return false;
        }
        VkClearValue clear[VKR_RP_ATTACH];
        uint32_t n = 0;
        for (uint32_t i = 0; i < d->colorCount; i++) {
            clear[n++] = d->color[i].clear;
        }
        if (d->hasDepth) {
            clear[n++] = d->depth.clear;
        }
        VkRenderPassBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = rp,
            .framebuffer = fb,
            .renderArea = {{0, 0}, {d->areaW, d->areaH}},
            .clearValueCount = n,
            .pClearValues = n ? clear : NULL,
        };
        vkCmdBeginRenderPass(c->cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        return true;
    }
    VkRenderingAttachmentInfo color[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const VkrPassAttach *a = &d->color[i];
        color[i] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = a->view,
            .imageLayout = a->layout,
            .loadOp = a->load,
            .storeOp = a->store,
            .clearValue = a->clear,
        };
    }
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencil = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (d->hasDepth) {
        depth.imageView = d->depth.view;
        depth.imageLayout = d->depth.layout;
        depth.loadOp = d->depth.load;
        depth.storeOp = d->depth.store;
        depth.clearValue = d->depth.clear;
        if (d->hasStencil) {
            stencil = depth;
            stencil.loadOp = d->depth.stencilLoad;
            stencil.storeOp = d->depth.stencilStore;
        }
    }
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {d->areaW, d->areaH}},
        .layerCount = 1,
        .colorAttachmentCount = d->colorCount,
        .pColorAttachments = color,
        .pDepthAttachment = d->hasDepth ? &depth : NULL,
        .pStencilAttachment = d->hasStencil ? &stencil : NULL,
    };
    g_vkr.cmdBeginRendering(c->cb, &ri);
    return true;
}

void vkr_pass_end(VkrCmdList *c)
{
    if (g_vkr.legacyPasses) {
        vkCmdEndRenderPass(c->cb);
    } else {
        g_vkr.cmdEndRendering(c->cb);
    }
}

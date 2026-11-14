/* vk_qprio.c: the game's queue at high global priority on Qualcomm's own
 * driver under Android (port/rhi/rhi_qprio.h; issue 123's second attempt).
 *
 * vkr_create_device hands its finished VkDeviceCreateInfo here.  Off the
 * row (rhi_pass_row.h rhi_vk_qprio_row), or on it where the device offers
 * no global priority extension or its queue family does not list high,
 * the device is created exactly as before.  On the row the queue's
 * VkDeviceQueueCreateInfo gets VkDeviceQueueGlobalPriorityCreateInfo at
 * VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR in front of its own chain, and the
 * extension (VK_KHR_global_priority, or VK_EXT_global_priority where KHR is
 * not listed) is added to the list.  A success is the grant: the
 * specification has no partial one.  A failure (VK_ERROR_NOT_PERMITTED for
 * a caller without the right, or any other) is followed by one more
 * creation without the structure and the extension, as before; only that
 * one's failure is the device's (VKR_CHECK).  One start line says which,
 * at every creation (a relaunch after a loss and a switch of the graphics
 * API ask again).
 *
 * ICO_VK_FAKE_QUEUE_PRIORITY (the tests alone, rhi_qprio.h): grant,
 * refuse, unoffered put any device on the row; refuse answers the asking
 * creation with VK_ERROR_NOT_PERMITTED without calling the driver. */
#include <stdio.h>
#include <stdlib.h>

#include "vk_internal.h"
#include "../rhi_driver_version.h"
#include "../rhi_pass_row.h"
#include "../rhi_qprio.h"

static char s_said[160] = "no global priority asked (the driver's default, medium)";

const char *vkr_qprio_said(void)
{
    return s_said;
}

/* "low, medium, high" */
static void priority_list(const VkrQprioOffer *o, char *out, size_t cap)
{
    size_t at = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < o->count && i < VK_MAX_GLOBAL_PRIORITY_SIZE_KHR && at < cap; i++) {
        const VkQueueGlobalPriorityKHR p = o->priorities[i];
        const char *w = p == VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR        ? "low"
                        : p == VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR   ? "medium"
                        : p == VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR     ? "high"
                        : p == VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR ? "realtime"
                                                                     : "another";
        const int n = snprintf(out + at, cap - at, "%s%s", i ? ", " : "", w);
        if (n < 0) {
            break;
        }
        at += (size_t)n;
    }
}

bool vkr_qprio_create_device(VkDeviceCreateInfo *ci, VkDeviceQueueCreateInfo *qci,
                             const char **exts, uint32_t cap, const VkrQprioOffer *offer)
{
    g_vkr.qprio = RHI_QPRIO_NOT_ASKED;
    g_vkr.qprioResult = 0;
    g_vkr.qprioWhy = NULL;
    g_vkr.qprioKhr = false;
    snprintf(s_said, sizeof s_said, "no global priority asked (the driver's default, medium)");
    const int fake = rhi_vk_qprio_fake(getenv("ICO_VK_FAKE_QUEUE_PRIORITY"));
#ifdef __ANDROID__
    const int android = 1;
#else
    const int android = 0;
#endif
    const char *row =
        fake != RHI_QPRIO_FAKE_NONE
            ? rhi_vk_qprio_row(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY, 1)
            : rhi_vk_qprio_row(g_vkr.props.vendorID, g_vkr.driverID, android);
    const bool hidden = fake == RHI_QPRIO_FAKE_UNOFFERED;
    bool hasHigh = false;
    for (uint32_t i = 0; i < offer->count && i < VK_MAX_GLOBAL_PRIORITY_SIZE_KHR; i++) {
        hasHigh = hasHigh || offer->priorities[i] == VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;
    }
    char list[96];
    priority_list(offer, list, sizeof list);
    char fakeSay[64] = "";
    if (fake != RHI_QPRIO_FAKE_NONE) {
        snprintf(fakeSay, sizeof fakeSay, " (ICO_VK_FAKE_QUEUE_PRIORITY=%s)",
                 rhi_vk_qprio_fake_word(fake));
    }
    const char *extName = NULL;
    const char *why = NULL;
    int ask = rhi_vk_qprio_plan(row != NULL, !hidden && offer->khr, !hidden && offer->ext,
                                !hidden && offer->queried, hasHigh, &extName, &why);
    if (ask && ci->enabledExtensionCount + 1u > cap) {
        ask = 0;
        why = "Qualcomm's own driver on Android, the device's extension list full";
    }
    if (!ask) {
        g_vkr.qprioWhy = why;
        VKR_LOG("queue priority: the driver's default (medium), high not asked: %s%s", why,
                fakeSay);
        return VKR_CHECK(vkCreateDevice(g_vkr.phys, ci, NULL, &g_vkr.device));
    }
    const bool khr = !hidden && offer->khr; /* the plan's choice of extension */
    VkDeviceQueueGlobalPriorityCreateInfoKHR gp = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR,
        .pNext = qci->pNext,
        .globalPriority = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
    };
    const void *qNext = qci->pNext;
    const uint32_t n = ci->enabledExtensionCount;
    exts[n] = extName;
    ci->ppEnabledExtensionNames = exts;
    ci->enabledExtensionCount = n + 1u;
    qci->pNext = &gp;
    const VkResult r = fake == RHI_QPRIO_FAKE_REFUSE
                           ? VK_ERROR_NOT_PERMITTED_KHR
                           : vkCreateDevice(g_vkr.phys, ci, NULL, &g_vkr.device);
    qci->pNext = qNext;
    if (r == VK_SUCCESS) {
        g_vkr.qprio = RHI_QPRIO_GRANTED;
        g_vkr.qprioKhr = khr;
        g_vkr.qprioWhy = rhi_vk_qprio_granted_why(khr);
        snprintf(s_said, sizeof s_said, "global priority high asked and granted (%s)", extName);
        VKR_LOG("queue priority: high, asked and granted (%s; through %s; queue family %u "
                "offers %s)%s",
                row, extName, g_vkr.queueFamily, offer->queried ? list : "a list not read",
                fakeSay);
        return true;
    }
    /* as before: no structure, no extension */
    ci->enabledExtensionCount = n;
    g_vkr.device = VK_NULL_HANDLE;
    g_vkr.qprio = RHI_QPRIO_REFUSED;
    g_vkr.qprioResult = (int32_t)r;
    g_vkr.qprioWhy = rhi_vk_qprio_refused_why((int32_t)r);
    snprintf(s_said, sizeof s_said,
             "global priority high asked and refused (%s): no global priority (the driver's "
             "default, medium)",
             rhi_vk_qprio_result_word((int32_t)r));
    VKR_LOG("queue priority: high asked and refused (VkResult %d, %s): the device made again "
            "without it, at the driver's default (medium) (%s; through %s)%s",
            (int)r, rhi_vk_qprio_result_word((int32_t)r), row, extName, fakeSay);
    return VKR_CHECK(vkCreateDevice(g_vkr.phys, ci, NULL, &g_vkr.device));
}

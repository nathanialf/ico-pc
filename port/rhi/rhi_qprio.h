/* rhi_qprio.h: the game's queue at high global priority (issue 123's second
 * attempt, v0.11.7): what port/rhi/vk/vk_qprio.c asks for at the device's
 * creation, its tests' fake and the words of its lines.  No Vulkan header:
 * built and tested on every host (port/rhi/test/rhi_vk_qprio_test.c).
 *
 * On the row (rhi_pass_row.h rhi_vk_qprio_row: Qualcomm's own driver on
 * Android) the queue is created with VkDeviceQueueGlobalPriorityCreateInfo
 * at VK_QUEUE_GLOBAL_PRIORITY_HIGH through VK_KHR_global_priority (or
 * VK_EXT_global_priority where KHR is not listed), where the queue family's
 * list of priorities holds high (or cannot be read).  REALTIME is never
 * asked.  A creation that fails with the priority (VK_ERROR_NOT_PERMITTED
 * for a caller without the right, or any other failure) is made once more
 * as before, without it.  The picture is the same either way: only the
 * queue's scheduling priority changes.  The analysis:
 * build-host/tmp/v0117/design/QPRIO.md (not in the repository). */
#ifndef PORT_RHI_RHI_QPRIO_H
#define PORT_RHI_RHI_QPRIO_H

#include <stdint.h>

/* RhiLimits.queuePriority: what the device's creation asked and got */
enum {
    RHI_QPRIO_NOT_ASKED = 0, /* the driver's default (medium); the why says why */
    RHI_QPRIO_GRANTED = 1,   /* high asked and granted */
    RHI_QPRIO_REFUSED = 2    /* high asked, the creation failed: made again without it */
};

/* ICO_VK_FAKE_QUEUE_PRIORITY's words (the tests alone): 0 none (unset,
 * empty or anything else); 1 "grant": the device on the row, the real offer
 * read and the real call made; 2 "refuse": the device on the row, the
 * creation that asks answered VK_ERROR_NOT_PERMITTED without calling the
 * driver (the fallback's creation is the real one); 3 "unoffered": the
 * device on the row with the offer read as none */
enum {
    RHI_QPRIO_FAKE_NONE = 0,
    RHI_QPRIO_FAKE_GRANT,
    RHI_QPRIO_FAKE_REFUSE,
    RHI_QPRIO_FAKE_UNOFFERED
};

int rhi_vk_qprio_fake(const char *value);
/* the fake's word for the lines ("grant"), "" for none */
const char *rhi_vk_qprio_fake_word(int fake);

/* The plan: 1 ask high, *extName the extension to enable
 * ("VK_KHR_global_priority" where khr, else "VK_EXT_global_priority");
 * 0 not asked, *why the reason (a static phrase).  row: the device is on
 * rhi_vk_qprio_row's row; khr, ext: the extensions are listed (khr only
 * where the device may take it: Vulkan 1.1 or the properties2 queries);
 * queried: the family's priorities were read; hasHigh: high among them
 * (read only where queried). */
int rhi_vk_qprio_plan(int row, int khr, int ext, int queried, int hasHigh, const char **extName,
                      const char **why);

/* The failed creation's VkResult in words ("not permitted" for
 * VK_ERROR_NOT_PERMITTED, -1000174001; "initialization failed", ...;
 * "another error" for a code not named) */
const char *rhi_vk_qprio_result_word(int32_t result);

/* The attempt ledger's reasons (static strings): granted through the
 * extension named (khr nonzero: VK_KHR_global_priority, else the EXT one),
 * and refused with the result's words */
const char *rhi_vk_qprio_granted_why(int khr);
const char *rhi_vk_qprio_refused_why(int32_t result);

#endif /* PORT_RHI_RHI_QPRIO_H */

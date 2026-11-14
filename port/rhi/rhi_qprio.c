/* rhi_qprio.c: the game's queue at high global priority (rhi_qprio.h). */
#include "rhi_qprio.h"

#include <ctype.h>
#include <stddef.h>

/* VkResult codes (vulkan_core.h; no Vulkan header here) */
#define QP_OUT_OF_HOST_MEMORY (-1)
#define QP_OUT_OF_DEVICE_MEMORY (-2)
#define QP_INITIALIZATION_FAILED (-3)
#define QP_DEVICE_LOST (-4)
#define QP_EXTENSION_NOT_PRESENT (-7)
#define QP_FEATURE_NOT_PRESENT (-8)
#define QP_TOO_MANY_OBJECTS (-10)
#define QP_NOT_PERMITTED (-1000174001)

static int word_is(const char *v, const char *w)
{
    size_t i = 0;
    for (; v[i] != '\0' && w[i] != '\0'; i++) {
        if (tolower((unsigned char)v[i]) != w[i]) {
            return 0;
        }
    }
    return v[i] == '\0' && w[i] == '\0';
}

int rhi_vk_qprio_fake(const char *value)
{
    if (value == NULL) {
        return RHI_QPRIO_FAKE_NONE;
    }
    if (word_is(value, "grant")) {
        return RHI_QPRIO_FAKE_GRANT;
    }
    if (word_is(value, "refuse")) {
        return RHI_QPRIO_FAKE_REFUSE;
    }
    if (word_is(value, "unoffered")) {
        return RHI_QPRIO_FAKE_UNOFFERED;
    }
    return RHI_QPRIO_FAKE_NONE;
}

const char *rhi_vk_qprio_fake_word(int fake)
{
    switch (fake) {
    case RHI_QPRIO_FAKE_GRANT:
        return "grant";
    case RHI_QPRIO_FAKE_REFUSE:
        return "refuse";
    case RHI_QPRIO_FAKE_UNOFFERED:
        return "unoffered";
    default:
        return "";
    }
}

int rhi_vk_qprio_plan(int row, int khr, int ext, int queried, int hasHigh, const char **extName,
                      const char **why)
{
    *extName = NULL;
    *why = NULL;
    if (!row) {
        *why = "not Qualcomm's own driver on Android";
        return 0;
    }
    if (!khr && !ext) {
        *why = "Qualcomm's own driver on Android, global queue priority not offered";
        return 0;
    }
    if (queried && !hasHigh) {
        *why = "Qualcomm's own driver on Android, the game's queue family does not list high";
        return 0;
    }
    *extName = khr ? "VK_KHR_global_priority" : "VK_EXT_global_priority";
    return 1;
}

const char *rhi_vk_qprio_result_word(int32_t result)
{
    switch (result) {
    case QP_NOT_PERMITTED:
        return "not permitted";
    case QP_INITIALIZATION_FAILED:
        return "initialization failed";
    case QP_OUT_OF_HOST_MEMORY:
        return "out of host memory";
    case QP_OUT_OF_DEVICE_MEMORY:
        return "out of device memory";
    case QP_DEVICE_LOST:
        return "device lost";
    case QP_EXTENSION_NOT_PRESENT:
        return "extension not present";
    case QP_FEATURE_NOT_PRESENT:
        return "feature not present";
    case QP_TOO_MANY_OBJECTS:
        return "too many objects";
    default:
        return "another error";
    }
}

const char *rhi_vk_qprio_granted_why(int khr)
{
    return khr ? "high granted through VK_KHR_global_priority: Qualcomm's own driver on Android"
               : "high granted through VK_EXT_global_priority: Qualcomm's own driver on Android";
}

const char *rhi_vk_qprio_refused_why(int32_t result)
{
    switch (result) {
    case QP_NOT_PERMITTED:
        return "high asked, the driver answered not permitted: the driver's default (medium)";
    case QP_INITIALIZATION_FAILED:
        return "high asked, the driver answered initialization failed: the driver's default "
               "(medium)";
    default:
        return "high asked, the device's creation failed with it: the driver's default (medium)";
    }
}

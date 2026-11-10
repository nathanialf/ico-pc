/* rhi_pass_row.c: the render pass driver row (rhi_pass_row.h). */
#include "rhi_pass_row.h"
#include "rhi_driver_version.h"
#include <ctype.h>
#include <string.h>

const char *rhi_vk_pass_row(uint32_t vendorId, uint32_t driverId, uint32_t apiVersion,
                            int windowsBuild)
{
    if (vendorId != RHI_VENDOR_INTEL || apiVersion > RHI_VK_PASS_ROW_INTEL_API) {
        return NULL;
    }
    /* the driver id when the driver gives one (Mesa's ANV is not the
     * row's), else the build: Intel's own driver is the Windows one */
    const int own =
        driverId != 0 ? driverId == RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS : windowsBuild != 0;
    if (!own) {
        return NULL;
    }
    return "Intel's Windows driver at Vulkan 1.3.215 or older (the final drivers for 6th to 10th "
           "gen graphics crash in vkCmdBeginRendering)";
}

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

int rhi_vk_pass_override(const char *value)
{
    static const char *const on[] = {"1", "true", "yes", "on"};
    static const char *const off[] = {"0", "false", "no", "off"};
    if (value == NULL) {
        return -1;
    }
    for (size_t i = 0; i < sizeof on / sizeof on[0]; i++) {
        if (word_is(value, on[i])) {
            return 1;
        }
        if (word_is(value, off[i])) {
            return 0;
        }
    }
    return -1;
}

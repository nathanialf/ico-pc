/* rhi_pass_row.c: the render pass driver row (rhi_pass_row.h). */
#include "rhi_pass_row.h"
#include "rhi_device_table.h"
#include <ctype.h>
#include <string.h>

/* The rows are the device table's (rhi_device_table.c intel-windows-1.3.215
 * and qualcomm-android): each predicate builds a Vulkan identity of its
 * arguments and reads its choice there, the reason its row's words
 * (port/rhi/test/rhi_device_table_equiv_test.c proves the answers equal to
 * the rules these functions held before, on every identity of its matrix) */
static RhiDeviceId passId(uint32_t vendorId, uint32_t driverId, uint32_t apiVersion, int os)
{
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = (uint8_t)os;
    id.vendorId = vendorId;
    id.driverId = driverId;
    id.apiVersion = apiVersion;
    return id;
}

const char *rhi_vk_pass_row(uint32_t vendorId, uint32_t driverId, uint32_t apiVersion,
                            int windowsBuild)
{
    /* the driver id when the driver gives one (Mesa's ANV is not the
     * row's), else the build: Intel's own driver is the Windows one */
    const RhiDeviceId id =
        passId(vendorId, driverId, apiVersion, windowsBuild ? RHI_DT_WINDOWS : RHI_DT_LINUX);
    const char *why = NULL;
    return rhi_device_table_choice(&id, RHI_DC_PASS_OBJECTS, &why) == RHI_DT_PASS_INTEL ? why
                                                                                        : NULL;
}

const char *rhi_vk_pass_row_qualcomm(uint32_t vendorId, uint32_t driverId, int android)
{
    /* rhi_driver_is_qualcomm_proprietary's rule (the table's QCOM_ID);
       Android's alone (the evidence) */
    const RhiDeviceId id = passId(vendorId, driverId, 0u, android ? RHI_DT_ANDROID : RHI_DT_LINUX);
    const char *why = NULL;
    return rhi_device_table_choice(&id, RHI_DC_PASS_OBJECTS, &why) == RHI_DT_PASS_QUALCOMM ? why
                                                                                           : NULL;
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

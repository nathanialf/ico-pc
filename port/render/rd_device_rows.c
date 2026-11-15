/* rd_device_rows.c: the renderer's pure reads of the device table
 * (rd_device_rows.h).  No rd__log, no backend: the equivalence test links
 * this file (port/rhi/test/rhi_device_table_equiv_test.c). */
#include "rd_device_rows.h"

#include <stddef.h>
#include <string.h>

#include "rd_vu_class.h"

int32_t rd_device_rows_value(const RhiDeviceChoices *current, int backend, bool liveLimits,
                             RhiDeviceChoice choice)
{
    if ((unsigned)choice >= RHI_DC_COUNT) {
        return 0;
    }
    if (liveLimits && current != NULL) {
        return current->value[choice];
    }
    /* no device: the identity of the backend alone (no vendor, no OS row:
       the zeroed limits the old rules read said no tile-based GPU and no
       phone on every OS) */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = (uint8_t)backend;
    id.os = RHI_DT_LINUX;
    return rhi_device_table_choice(&id, choice, NULL);
}

bool rd_device_rows_adreno(int32_t profileClass)
{
    return profileClass == RHI_DT_CLASS_ADRENO_QUALCOMM ||
           profileClass == RHI_DT_CLASS_ADRENO_TURNIP;
}

const char *rd_device_rows_reason(const char *rowId, RhiDeviceChoice choice)
{
    size_t n = 0;
    const RhiDeviceRow *rows = rhi_device_table_rows(&n);
    const int r = rhi_device_table_find(rowId);
    if (r < 0 || (size_t)r >= n) {
        return NULL;
    }
    for (uint8_t s = 0; s < rows[r].settingCount; s++) {
        if (rows[r].settings[s].choice == (uint16_t)choice) {
            return rows[r].settings[s].reason;
        }
    }
    return NULL;
}

bool rd__driver_is_turnip(const RhiLimits *lim)
{
    if (lim == NULL) {
        return false;
    }
    /* Mesa's Turnip, or a build of it that renamed itself: the Vulkan
     * driver id (VK_DRIVER_ID_MESA_TURNIP, 18) or, from a driver without
     * VK_KHR_driver_properties (id 0), the name in the driver text (issue
     * 209's "PurpleVK public driver", Turnip 26.3.0-devel with id 18, names
     * itself neither): the table's Turnip test, the turnip row's match */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_OS_BUILD;
    id.vendorId = lim->vendorId;
    id.driverId = lim->driverId;
    id.driverText = lim->driverText;
    return rhi_device_driver_test(RHI_DT_DRV_TURNIP, &id) != 0;
}

bool rd__profile_vendor_adreno(uint32_t vendorId, bool moltenvk)
{
    /* the profile class of a Vulkan device of that vendor id (Qualcomm's,
       not through MoltenVK: the rows adreno and turnip-adreno) */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_OS_BUILD;
    id.moltenvk = moltenvk ? 1u : 0u;
    id.vendorId = vendorId;
    return rd_device_rows_adreno(rhi_device_table_choice(&id, RHI_DC_PROFILE_CLASS, NULL));
}

bool rd__vu_class_measured(const char *name)
{
    /* the row powervr-dxt-48-1536's device name (RD_VU_CLASS_MEASURED_NAME)
       on an Imagination GPU under Vulkan */
    RhiDeviceId id;
    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_OS_BUILD;
    id.vendorId = 0x1010u;
    id.deviceName = name;
    return rhi_device_table_choice(&id, RHI_DC_VU_COMPUTE_CLASS, NULL) == RHI_DT_VUCOMP_MEASURED;
}

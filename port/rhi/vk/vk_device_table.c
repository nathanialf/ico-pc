/* vk_device_table.c: the device table's start-up lines on Vulkan
 * (../rhi_device_table.h; docs/port/DEVICE_TABLE.md).
 *
 * Once a device is picked (vk_device.c, after its driver text and the
 * MoltenVK test): the device's identity (the ids, its name, its driver's id,
 * text and version, the Vulkan version, the build's OS, MoltenVK, a driver
 * package the Android build loaded) through the table, then one line naming
 * the rows in force out of the table's count (and the rows that matched with
 * every setting overridden), and a line for each row in force with what it
 * sets, its issue, the release it came in and the condition to revisit it.
 * The rows' clause is kept for the renderer's summary line
 * (rhi_device_table_note; rd_core.c rd_summary_text).
 *
 * Read only: no choice is taken from the table here (the call sites keep
 * their own rules until each moves onto the table), so the tests' fakes are
 * not applied to these lines. */
#include "vk_internal.h"
#include "../rhi_device_table.h"
#include <string.h>

void vkr_device_table_log(const char *driverText, bool customDriver)
{
    RhiDeviceId id;
    RhiDeviceChoices c;
    char line[2048];
    size_t rowCount;

    memset(&id, 0, sizeof id);
    id.backend = RHI_DT_VULKAN;
    id.os = RHI_DT_OS_BUILD;
    id.moltenvk = g_vkr.moltenvk ? 1u : 0u;
    id.customDriver = customDriver ? 1u : 0u;
    id.vendorId = g_vkr.props.vendorID;
    id.deviceId = g_vkr.props.deviceID;
    id.driverId = g_vkr.driverID;
    id.apiVersion = g_vkr.props.apiVersion;
    id.deviceName = g_vkr.props.deviceName;
    id.driverText = driverText;
    id.driverVersion = g_vkr.driverVersion[0] ? g_vkr.driverVersion : NULL;
    rhi_device_table_eval(&id, NULL, &c);
    rhi_device_table_header(line, sizeof line, &id, &c);
    VKR_LOG("%s", line);
    rhi_device_table_rows(&rowCount);
    for (size_t r = 0; r < rowCount && r < RHI_DT_MAX_ROWS; r++) {
        if (c.applied[r]) {
            rhi_device_table_row_line(line, sizeof line, &c, (int)r);
            VKR_LOG("%s", line);
        }
    }
    rhi_device_table_note(&c);
}

/* vk_device_table.c: the device table on Vulkan (../rhi_device_table.h;
 * docs/port/DEVICE_TABLE.md).
 *
 * Once a device is picked (vk_device.c vkr_pick_device, after its driver
 * text and the MoltenVK test): the device's identity (the ids, its name, its
 * driver's id, text and version, the Vulkan version, the build's OS,
 * MoltenVK, a driver package the Android build loaded) and the tests'
 * environment fakes the backend's call sites read (each with the scope it
 * had before the table: rhi_device_table.h RhiDeviceFakeEnv) through the
 * table once, kept in g_vkr.devTable for the call sites (the tile rule, the
 * dynamic state, the memory budget, the driver rows, the subpass class, the
 * attachment read, the robustness ask, the VU rows, the serial compiles).
 * Then one line naming the rows in force out of the table's count (and the
 * rows that matched with every setting overridden), and a line for each row
 * in force with what it sets, its issue, the release it came in and the
 * condition to revisit it (a setting a test's fake replaced says so).  The rows' clause is kept for the renderer's
 * summary line (rhi_device_table_note; rd_core.c rd_summary_text).
 *
 * ICO_RD_VU_COMPACT_ARM is the renderer's fake (rd_vu_compact.c reads it
 * where it decides): not applied here. */
#include "vk_internal.h"
#include "../rhi_device_table.h"
#include <stdlib.h>
#include <string.h>

static void vkr_device_table_line(const char *line, void *user)
{
    (void)user;
    VKR_LOG("%s", line);
}

void vkr_device_table_start(const char *driverText, bool customDriver)
{
    RhiDeviceId id;
    RhiDeviceFakeEnv env;

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
    memset(&env, 0, sizeof env);
    env.vendor = getenv("ICO_VK_FAKE_VENDOR");
    env.passRow = getenv("ICO_VK_FAKE_PASS_ROW");
    env.adreno = getenv("ICO_VK_FAKE_ADRENO");
    env.serial = getenv("ICO_VK_FAKE_SERIAL");
    env.noPrepass = getenv("ICO_VK_FAKE_NO_PREPASS");
    rhi_device_fakes_parse(&env, &g_vkr.devFakes);
    g_vkr.devTable = rhi_device_table_start(&id, &g_vkr.devFakes, vkr_device_table_line, NULL);
}

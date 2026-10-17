/* rhi_driver_version.c: the driver version text and the drivers' rules
 * (rhi_driver_version.h). */
#include "rhi_driver_version.h"
#include <stdio.h>
#include <string.h>

char *rhi_driver_version_text(uint32_t vendorId, uint32_t driverId, uint32_t driverVersion,
                              char *buf, size_t n)
{
    const uint32_t v = driverVersion;
    if (n == 0) {
        return buf;
    }
    const int nvidia = driverId != 0 ? driverId == RHI_DRIVER_ID_NVIDIA_PROPRIETARY
                                     : vendorId == RHI_VENDOR_NVIDIA;
    const int intelWin = driverId != 0 ? driverId == RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS
#ifdef _WIN32
                                       : vendorId == RHI_VENDOR_INTEL;
#else
                                       : 0;
#endif
    if (nvidia) {
        const unsigned major = (v >> 22) & 0x3ffu, minor = (v >> 14) & 0xffu;
        const unsigned third = (v >> 6) & 0xffu, fourth = v & 0x3fu;
        if (third != 0 || fourth != 0) {
            snprintf(buf, n, "%u.%02u.%u.%u", major, minor, third, fourth);
        } else {
            snprintf(buf, n, "%u.%02u", major, minor);
        }
    } else if (intelWin) {
        snprintf(buf, n, "%u.%u", v >> 14, v & 0x3fffu);
    } else {
        snprintf(buf, n, "%u.%u.%u", v >> 22, (v >> 12) & 0x3ffu, v & 0xfffu);
    }
    return buf;
}

int rhi_driver_is_qualcomm_proprietary(uint32_t vendorId, uint32_t driverId)
{
    return vendorId == RHI_VENDOR_QUALCOMM &&
           (driverId == RHI_DRIVER_ID_QUALCOMM_PROPRIETARY || driverId == 0);
}

int rhi_driver_is_moltenvk(uint32_t vendorId, uint32_t driverId, int moltenvkBuild)
{
    return driverId == RHI_DRIVER_ID_MOLTENVK ||
           (moltenvkBuild && vendorId == RHI_VENDOR_APPLE && driverId == 0);
}

uint32_t rhi_metal_color_bytes(const char *deviceName)
{
    if (deviceName == NULL || strncmp(deviceName, "Apple ", 6) != 0) {
        return 0u;
    }
    const char *p = deviceName + 6;
    if (*p == 'M' && p[1] >= '1' && p[1] <= '9') {
        return 128u; /* the M-series: Apple7 and later */
    }
    if (*p != 'A' || p[1] < '0' || p[1] > '9') {
        return 0u;
    }
    unsigned n = 0;
    for (p++; *p >= '0' && *p <= '9' && n < 1000u; p++) {
        n = n * 10u + (unsigned)(*p - '0');
    }
    return n <= 10u ? 32u : n <= 13u ? 64u : 128u;
}

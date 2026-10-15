/* rhi_driver_version.c: the driver version text (rhi_driver_version.h). */
#include "rhi_driver_version.h"
#include <stdio.h>

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

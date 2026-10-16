/* rhi_driver_version_test.c: the driver version text
 * (port/rhi/rhi_driver_version.c) for each packing: NVIDIA's own driver
 * (by driver id, and by vendor id when the id is unknown), Mesa's NVK on an
 * NVIDIA GPU (the Vulkan packing), Intel's Windows driver, and the Vulkan
 * packing of everyone else.  Exit 0 on success, 1 on a failure. */
#include "rhi_driver_version.h"
#include "../../test/ico_check.h"

static const char *text(uint32_t vendor, uint32_t id, uint32_t v)
{
    static char buf[32];
    return rhi_driver_version_text(vendor, id, v, buf, sizeof buf);
}

int main(void)
{
    /* NVIDIA 581.57: (581 << 22) | (57 << 14) */
    const uint32_t nv58157 = (581u << 22) | (57u << 14);
    CHECK(nv58157 == 0x914E4000u);
    CHECK_STR(text(RHI_VENDOR_NVIDIA, RHI_DRIVER_ID_NVIDIA_PROPRIETARY, nv58157), "581.57");
    CHECK_STR(text(RHI_VENDOR_NVIDIA, 0, nv58157), "581.57");
    /* a one-digit minor keeps NVIDIA's two digits (581.08) */
    CHECK_STR(text(RHI_VENDOR_NVIDIA, 0, (581u << 22) | (8u << 14)), "581.08");
    /* the third and fourth parts when set */
    CHECK_STR(text(RHI_VENDOR_NVIDIA, 0, (580u << 22) | (82u << 14) | (7u << 6) | 3u),
              "580.82.7.3");
    /* NVK (Mesa's driver, id 24) on an NVIDIA GPU: Mesa 25.1.4 */
    const uint32_t mesa = (25u << 22) | (1u << 12) | 4u;
    CHECK_STR(text(RHI_VENDOR_NVIDIA, 24, mesa), "25.1.4");
    /* Intel's Windows driver: 101.5333 */
    CHECK_STR(text(RHI_VENDOR_INTEL, RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, (101u << 14) | 5333u),
              "101.5333");
    /* Intel on Mesa (ANV, id 6): the Vulkan packing */
    CHECK_STR(text(RHI_VENDOR_INTEL, 6, mesa), "25.1.4");
    /* AMD's driver (id 1): 2.0.302 */
    CHECK_STR(text(0x1002u, 1, (2u << 22) | 302u), "2.0.302");
    /* Qualcomm's Adreno driver (id 8): the Vulkan packing, 512.800.72 (issue 89) */
    CHECK_STR(text(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY,
                   (512u << 22) | (800u << 12) | 72u),
              "512.800.72");
    /* the attachment read's rule (vk_device.c): Qualcomm's own driver, by
       id or with the id unknown; not Turnip (Mesa, id 18) on the same GPU,
       not another vendor */
    CHECK(rhi_driver_is_qualcomm_proprietary(RHI_VENDOR_QUALCOMM,
                                             RHI_DRIVER_ID_QUALCOMM_PROPRIETARY));
    CHECK(rhi_driver_is_qualcomm_proprietary(RHI_VENDOR_QUALCOMM, 0));
    CHECK(!rhi_driver_is_qualcomm_proprietary(RHI_VENDOR_QUALCOMM, 18));
    CHECK(!rhi_driver_is_qualcomm_proprietary(0x13B5u, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY));
    CHECK(!rhi_driver_is_qualcomm_proprietary(RHI_VENDOR_NVIDIA, 0));
    CHECK(rhi_driver_is_moltenvk(RHI_VENDOR_APPLE, RHI_DRIVER_ID_MOLTENVK, 0));
    CHECK(rhi_driver_is_moltenvk(0x1002u, RHI_DRIVER_ID_MOLTENVK, 0));
    CHECK(rhi_driver_is_moltenvk(RHI_VENDOR_APPLE, 0, 1));
    CHECK(!rhi_driver_is_moltenvk(RHI_VENDOR_APPLE, 0, 0));
    CHECK(!rhi_driver_is_moltenvk(RHI_VENDOR_APPLE, 3, 1));
    CHECK(!rhi_driver_is_moltenvk(RHI_VENDOR_NVIDIA, 4, 1));
    /* a short buffer is cut and terminated */
    {
        char small[4];
        rhi_driver_version_text(RHI_VENDOR_NVIDIA, 0, nv58157, small, sizeof small);
        CHECK_STR(small, "581");
    }
    return ico_check_summary("rhi_driver_version");
}

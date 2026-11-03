/* rhi_driver_version_test.c: the driver version text
 * (port/rhi/rhi_driver_version.c) for each packing: NVIDIA's own driver
 * (by driver id, and by vendor id when the id is unknown), Mesa's NVK on an
 * NVIDIA GPU (the Vulkan packing), Intel's Windows driver, and the Vulkan
 * packing of everyone else; the Qualcomm and MoltenVK rules; the colour
 * bytes a pixel of the Apple GPUs MoltenVK names (rhi_metal_color_bytes); the
 * Mesa version of a Mesa driver's driverInfo (rhi_mesa_version_parse).
 * Exit 0 on success, 1 on a failure. */
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
    /* Metal's colour bytes a pixel by family (Metal Feature Set Tables):
       A9 and A10 (Apple3) 32, A11 to A13 64, A14 and the M-series 128;
       an Intel or AMD GPU of a Mac and a null name state none */
    CHECK(rhi_metal_color_bytes("Apple A9 GPU") == 32u);
    CHECK(rhi_metal_color_bytes("Apple A10X GPU") == 32u);
    CHECK(rhi_metal_color_bytes("Apple A11 GPU") == 64u);
    CHECK(rhi_metal_color_bytes("Apple A13 GPU") == 64u);
    CHECK(rhi_metal_color_bytes("Apple A14 GPU") == 128u);
    CHECK(rhi_metal_color_bytes("Apple A17 Pro GPU") == 128u);
    CHECK(rhi_metal_color_bytes("Apple M1") == 128u);
    CHECK(rhi_metal_color_bytes("Apple M3 Max") == 128u);
    CHECK(rhi_metal_color_bytes("AMD Radeon Pro 5500M") == 0u);
    CHECK(rhi_metal_color_bytes("Intel(R) UHD Graphics 630") == 0u);
    CHECK(rhi_metal_color_bytes("Apple Paravirtual device") == 0u);
    CHECK(rhi_metal_color_bytes(NULL) == 0u);
    /* the Mesa version out of driverName and driverInfo, in the forms Mesa
       drivers write */
    {
        RhiMesaVersion v;
        char t[32];
        /* Turnip built from Mesa's main branch (Weab-chan/freedreno_turnip-CI
           25.3.0-devel, the port's own log forms) */
        CHECK(rhi_mesa_version_parse("turnip Mesa driver", "Mesa 25.3.0-devel (git-bbdd688)", &v));
        CHECK(v.known && v.major == 25 && v.minor == 3 && v.patch == 0 && v.devel);
        CHECK_STR(rhi_mesa_version_text(&v, t, sizeof t), "Mesa 25.3.0-devel");
        /* the issue 163 tablet's Turnip (driverVersion 26.2.99, the number
           of a build before 26.3.0) */
        CHECK(
            rhi_mesa_version_parse("turnip Mesa driver", "Mesa 26.3.0-devel (git-a995eaeda4)", &v));
        CHECK(v.major == 26 && v.minor == 3 && v.patch == 0 && v.devel);
        /* a release */
        CHECK(rhi_mesa_version_parse("turnip Mesa driver", "Mesa 24.2.8", &v));
        CHECK(v.major == 24 && v.minor == 2 && v.patch == 8 && !v.devel);
        CHECK_STR(rhi_mesa_version_text(&v, t, sizeof t), "Mesa 24.2.8");
        /* llvmpipe of a distribution's package (this host's log): the
           package revision is no devel mark */
        CHECK(rhi_mesa_version_parse("llvmpipe", "Mesa 25.0.7-2 (LLVM 19.1.7)", &v));
        CHECK(v.major == 25 && v.minor == 0 && v.patch == 7 && !v.devel);
        /* a release candidate is not devel */
        CHECK(rhi_mesa_version_parse("turnip Mesa driver", "Mesa 26.0.0-rc2 (git-1a2b3c4)", &v));
        CHECK(v.major == 26 && v.minor == 0 && v.patch == 0 && !v.devel);
        /* the word in the name's place too (a text of name and info joined),
           other cases and separators, a "v", two parts only */
        CHECK(rhi_mesa_version_parse(NULL, "turnip Mesa driver Mesa 25.1.0", &v));
        CHECK(v.major == 25 && v.minor == 1 && v.patch == 0);
        CHECK(rhi_mesa_version_parse(NULL, "mesa-25.1", &v));
        CHECK(v.major == 25 && v.minor == 1 && v.patch == 0 && !v.devel);
        CHECK(rhi_mesa_version_parse(NULL, "MESA v24.2.8-DEVEL", &v));
        CHECK(v.major == 24 && v.minor == 2 && v.patch == 8 && v.devel);
        /* a build that writes the bare version under a Mesa driver name */
        CHECK(rhi_mesa_version_parse("turnip Mesa driver", "25.3.0-devel", &v));
        CHECK(v.major == 25 && v.minor == 3 && v.devel);
        /* a Mesa word with no number after it, then one that has */
        CHECK(rhi_mesa_version_parse(NULL, "Mesa driver, Mesa 25.2.1", &v));
        CHECK(v.major == 25 && v.minor == 2 && v.patch == 1);
        /* none: Qualcomm's driver, NVIDIA's, empty, a bare version under a
           non-Mesa name, a lone major, NULL */
        CHECK(!rhi_mesa_version_parse("Qualcomm Technologies Inc. Adreno Vulkan Driver",
                                      "Vulkan Driver Compiler Version: E031.41.03.47", &v));
        CHECK(!v.known);
        CHECK_STR(rhi_mesa_version_text(&v, t, sizeof t), "");
        CHECK(!rhi_mesa_version_parse("NVIDIA", "581.57", &v));
        CHECK(!rhi_mesa_version_parse("turnip Mesa driver", "", &v));
        CHECK(!rhi_mesa_version_parse("turnip Mesa driver", "Mesa 25", &v));
        CHECK(!rhi_mesa_version_parse(NULL, NULL, &v));
        CHECK(!rhi_mesa_version_parse(NULL, "Mesa", &v));
        /* a short buffer is cut and terminated */
        CHECK(rhi_mesa_version_parse(NULL, "Mesa 25.3.0-devel", &v));
        rhi_mesa_version_text(&v, t, 5);
        CHECK_STR(t, "Mesa");
    }
    /* a short buffer is cut and terminated */
    {
        char small[4];
        rhi_driver_version_text(RHI_VENDOR_NVIDIA, 0, nv58157, small, sizeof small);
        CHECK_STR(small, "581");
    }
    return ico_check_summary("rhi_driver_version");
}
